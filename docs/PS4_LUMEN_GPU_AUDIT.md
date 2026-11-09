# Lumen Emu — contribuições potenciais para a fase 4 GPU PS4

Implementação em andamento 2026-11-08: [checkpoint Lumen](PS4_LUMEN_INTEGRATION.md) registra adaptações de SMRD/ALU, sampler e diagnóstico GNM **ainda não compiladas ou testadas**. Integração P0..P15 INCOMPLETA; testes adiados até a integração solicitada estar completa. As aprovações anteriores abaixo não validam este novo código.

Data da auditoria: 2026-11-08. Referência local: `C:/Users/rodri/Dev1/Projetos/lumen-emu-main`. **Análise estática/documental concluída; nenhuma contribuição foi integrada ao código.** Lumen foi mantido somente leitura. Nenhum build, teste, jogo, APK ou driver Lumen foi executado/instalado. A fase 4 do Kyty continua parcial.

## Conclusão técnica

O maior valor imediato é **semântica GCN + contratos de recursos/sincronização + fixtures**, especialmente para o próximo sampler/IMAGE_SAMPLE_L. O núcleo Gen4 oferece referências PS4 mais diretas que o motor Gen5. A infraestrutura móvel traz casos úteis de publicação CPU/GPU, reutilização protegida por conclusão, detiling e diagnóstico. Nenhum desses achados justifica substituir os backends Vulkan/D3D12 ou importar um segundo emulador/processo.

O Kyty já tem IMAGE_LOAD RGBA8 linear e dois buffers/uma textura por VS e PS, com 25 testes e readbacks documentados em [PS4_TEXTURES.md](PS4_TEXTURES.md). Também possui IR de IMAGE_SAMPLE e caches de sampler Vulkan/D3D12. Portanto, o próximo trabalho é **normalizar e ligar o sampler PS4 ao caminho existente**, não criar outra IR ou outro cache genérico. O relatório abaixo distingue capacidades da referência, lacunas do perfil PS4 e trabalho de adaptação.

## 1. Identidade da referência e alcance da leitura

Não existe `.git` na raiz examinada; não há HEAD local verificável. Os manifests declaram:

| Árvore | Origem declarada | Uso correto nesta auditoria |
|---|---|---|
| `source/gen4` | shadPS4 via shadps4-arm64, revisão `be6bc2e9c60799e071dd2fafa6216e8d80ec619c` | Referência principal de ISA/registradores/PM4/recursos PS4 |
| `source/gen4/gen5/engine` | KytyPS5 `fe3857a1715551e4f554b2ec29cc1ca3286287a1` | Motor PS5 direto, adaptações de host e regressões compartilháveis; não prova GCN PS4 |
| `source/gen4/gen5/compatibility` | Import original KytyPS5 `b3e419ff1101999525fa2d061ada1d102cf788b1` e seleções posteriores | Caminho de compatibilidade; conferir seleção real do build antes de estudar um comportamento |
| `source/emulator/.../Graphics` | Caminho legado Lumen/Kyty, com contratos/probes adicionais | Ideias isoladas de staging, layouts, cache e testes; não assumir que seja o renderer ativo Gen4 |

`source/gen4/gen5/CMakeLists.txt:55..61` seleciona `engine/src` para Android com LUMEN_PS5_DIRECT_FEX; os outros casos usam a preparação de compatibility. Esse embedding restringe plataformas a macOS/Android ARM64. O README descreve processos separados por geração e dashboard não incluído no snapshot público. Esse arranjo não substitui o runtime interno único exigido pelo Kyty/UWP.

Inventário dirigido dos eixos P0..P15: shader_recompiler/video_core/testes Gen4, gráficos/testes Gen5 engine, gráficos legados e proveniência/licenças. **Não é leitura linha a linha de todo o repositório ou auditoria de todos os terceiros.** `build/lumen-audit/snapshot.json` registra 742 arquivos com SHA-256 e tamanho; a releitura dos hashes confirmou que a referência permaneceu igual. Fingerprint do manifesto ordenado: `12ea8c08998bac9d1d4552109f0d42c82bc6b3e82aebb9008aad9543831a148a`.

Comparação textual de `engine/src/graphics` com `KytyPS5/src/graphics`: 216 caminhos comuns; 48 iguais após normalizar CRLF/LF, 168 com texto diferente, 15 arquivos apenas na árvore engine examinada. Diferença textual **não significa melhoria, autoria Lumen ou mudança semântica**: as bases/versionamentos diferem. A árvore engine não contém o HostShaderFeatures.h presente no Kyty atual; importar o emissor inteiro pode inclusive remover trabalho mais recente daqui.

## 2. Mapa de leitura verificável

Aliases usados nas tabelas: **G4**=`source/gen4/src`; **T4**=`source/gen4/tests`; **G5**=`source/gen4/gen5/engine/src`; **LEG**=`source/emulator`; todos relativos à raiz Lumen indicada acima. Caminhos e símbolos são evidências de código, não de execução.

| ID | Arquivos/símbolos examinados | Contribuição principal |
|---|---|---|
| L01 | [G4/video_core/amdgpu/resource.h](../../lumen-emu-main/source/gen4/src/video_core/amdgpu/resource.h): Sampler, ClampMode, LodBias/MinLod/MaxLod; pixel_format.h/tiling.h | Campos GCN, filtros, clamp, LOD e layouts |
| L02 | [G4/video_core/texture_cache/sampler.cpp](../../lumen-emu-main/source/gen4/src/video_core/texture_cache/sampler.cpp): Sampler::Sampler; texture_cache.cpp:GetSampler; renderer_vulkan/liverpool_to_vk.* | Tradução de sampler e distinção de capacidades host |
| L03 | [G4/shader_recompiler/frontend/translate/vector_memory.cpp](../../lumen-emu-main/source/gen4/src/shader_recompiler/frontend/translate/vector_memory.cpp): BUFFER_LOAD, IMAGE_LOAD, IMAGE_SAMPLE; frontend/decode.cpp/opcodes.h/instruction.h | ISA de memória/imagem e operandos/modificadores |
| L04 | [G4/video_core/renderer_vulkan/image_binding.h](../../lumen-emu-main/source/gen4/src/video_core/renderer_vulkan/image_binding.h): IsImageBindingValid/IsIndexedImageBindingValid; T4/image_binding_contract_test.cpp | Validação de formato/view, bindings nulos e casos negativos |
| L05 | [G4/shader_recompiler/frontend/fetch_shader.cpp](../../lumen-emu-main/source/gen4/src/shader_recompiler/frontend/fetch_shader.cpp): ParseFetchShader; fetch_shader.h:VertexAttribute/GetSharp; T4/sharp_fetch_contract_test.cpp | Fetch externo, V#, typed loads, instancing e offsets |
| L06 | G4/shader_recompiler/frontend/translate/{scalar_alu,scalar_flow,vector_alu,data_share,export}.cpp; control_flow_graph.cpp/structured_control_flow.cpp | EXEC/VCC, CFG, ALU, LDS, exports e demais famílias |
| L07 | [G4/shader_recompiler/frontend/translate/vector_interpolation.cpp](../../lumen-emu-main/source/gen4/src/shader_recompiler/frontend/translate/vector_interpolation.cpp): V_INTERP_P1/P2/MOV, GetInterpolation, DefaultValTable | Flat/default, sample/centroid e cálculo manual de coeficientes |
| L08 | G4/shader_recompiler/ir/passes/{resource_discover_pass.cpp,resource_patching_pass.cpp,srt.h,lower_buffer_format_to_raw.cpp}; T4/resource_tracking_contract_test.cpp, srt_flatten_contract_test.cpp | Descriptors indiretos, especialização, materialização e formatos |
| L09 | [G4/video_core/texture_cache/tile_manager.cpp](../../lumen-emu-main/source/gen4/src/video_core/texture_cache/tile_manager.cpp): RefreshScratchCompletions/GetTilingPipeline/TileImage; image_info.cpp; host_shaders/tiling.comp | Layouts PS4, subrecursos e scratch de detiling |
| L10 | [G4/video_core/texture_cache/image_upload_barrier.h](../../lumen-emu-main/source/gen4/src/video_core/texture_cache/image_upload_barrier.h): GetImageUploadSourceBarrier; T4/detile_source_contract_test.cpp | Dependência correta do produtor para o detiler |
| L11 | [G4/video_core/guest_publication_snapshots.h](../../lumen-emu-main/source/gen4/src/video_core/guest_publication_snapshots.h): Track/ObserveCpuChanges/RecordBackingWrite/UnmapRange; guest_publication_inspection.h | Identidade/baseline e publicação de memória sem atribuir permissões |
| L12 | G4/video_core/buffer_cache/{cpu_write_journal.h,source_page_tracker.h,buffer_cache.cpp,buffer_pending_write_resolve.h,buffer_consumer_barrier.h}; page_manager.* | Escritas CPU, dirty ranges, consumidores e readbacks |
| L13 | [G4/video_core/amdgpu/liverpool.cpp](../../lumen-emu-main/source/gen4/src/video_core/amdgpu/liverpool.cpp): ProcessGraphics/ProcessCompute/SubmitGfx; pm4_*.h/regs*.h; core/libraries/gnmdriver/gnmdriver.cpp | Estado PM4, CCB/indiretos, draws, dispatch e waits |
| L14 | G4/video_core/renderer_vulkan/{vk_rasterizer.cpp,vk_graphics_pipeline.cpp,primitive_expansion.h,render_end_attachment_barrier.h}; T4/primitive_expansion_contract_test.cpp,rect_list_contract_test.cpp | Draws gerais, topologias, depth/stencil e attachment lifetime |
| L15 | [G4/video_core/renderer_vulkan/vk_scheduler.cpp](../../lumen-emu-main/source/gen4/src/video_core/renderer_vulkan/vk_scheduler.cpp): Wait/retirement; vk_pipeline_cache.* | Timeline, submissão, operações diferidas e persistência |
| L16 | G4/core/libraries/videoout/{video_out.cpp,driver.cpp}; renderer_vulkan/{vk_presenter.cpp,vk_swapchain_policy.h,frame_ledger.h,bounded_draw_diagnostics.h} | Registro/flip/vblank, apresentação e rastreabilidade |
| L17 | [LEG/include/Emulator/Graphics/UploadStagingArena.h](../../lumen-emu-main/source/emulator/include/Emulator/Graphics/UploadStagingArena.h); ShaderSourceSnapshotCache.h; TextureMipSnapshotCopy.h; TextureThin1dReinterpretCopy.h; TextureBc7Contract.h | Contratos pequenos de propriedade, gerações, layouts e staging |
| L18 | LEG/src/Graphics/Metal/{MetalVulkanCore.mm,MetalVulkanCommands.mm,MetalSoftwareTessellatorSource.inc}; G5/graphics/shader/recompiler/backend/spirv/* | Referência de fallback/capacidades em host restrito; não port direto D3D12 |

Os caminhos entre chaves representam arquivos individuais, não comandos PowerShell. A validação documental confere os destinos de links e o inventário; símbolos/trechos foram pesquisados com rg.

## 3. Contribuições por parte da fase 4

| Parte | O que o Lumen pode contribuir | Estado Kyty e adaptação necessária | Dependências/prioridade |
|---|---|---|---|
| P0 — contrato | Ledger por frame/draw/binding, resultado separado de metadata, limites de captura e testes negativos (L16) | Há probes/readbacks e stats GNM; acrescentar correlação entre comando, shader, recurso e imagem sem ler payload extra ou inundar logs | Imediata como apoio aos novos incrementos |
| P1 — recursos | Descriptors GCN, checagem de formato/view, indireção e identidade de backing (L01/L04/L08/L11) | Perfil PS4 já tem recursos diretos; faltam tabelas, null resources, múltiplas views e alias. Reutilizar ResourceTracking/materialização atuais | P0; alta, incremental |
| P2 — loads escalares | scalar_memory.cpp + SRT/resource tracking (L03/L08) | Expandir SMRD além do DWORDX4 direto, preservando offsets GCN e grupos; integrar ao runtime antes de prometer constantes gerais | P1; alta conforme fixture |
| P3 — fetch/draw | ParseFetchShader, typed V#, instancing, índices/indiretos e expansão de primitivas (L05/L13/L14) | Fetch inline MUBUF já existe. Implementar MTBUF/formatos, retorno de fetch externo e index buffers separadamente; não trocar o decoder RDNA2 | P1 + formatos; alta/média |
| P4 — ISA/lanes | EXEC/VCC, comparações vetoriais, branches, ALU e testes numéricos (L06) | Frontend GCN ainda restrito; IR atual já possui muitas operações. Priorizar VOPC/EXEC com definição/uso/merge e prova por lane | P0; alta para shaders gerais |
| P5 — interpolação | P1 manual com coeficientes reais, P2/MOV, default/flat e qualificadores (L07) | Hoje pares adjacentes smooth/linear central. Não remover a restrição sem representar intermediários e provoking vertex corretamente | P4/ABI e capacidades; média |
| P6 — layouts | TileManager/ImageInfo, tabelas de formatos, thin1d/BC/mips e contratos de scratch (L01/L09/L10/L17) | Tiler Prospero já existe no Kyty, mas não decodifica automaticamente layout Liverpool. Introduzir um layout GCN bem definido por vez | P1 + sizing/fixtures; alta após sampler |
| P7 — sampling | Sampler de quatro DWORDs, filtros/clamp/LOD/border e IMAGE_SAMPLE (L01/L02/L03) | IMAGE_LOAD já validado; IR e caches Vulkan/D3D12 de sampler já existem. Falta frontend/descriptor/binding GCN no diagnóstico | P1 + textura atual; **próximo incremento** |
| P8 — raster | Depth/stencil, formatos de RT, múltiplos attachments, export/kill e topologias (L14) | Raster PS4 ainda RGBA8, sem depth/MSAA. Mapear registradores para contrato neutro e os dois hosts, uma cena por estado | P1/P6 + exports; média |
| P9 — PM4/sync | Liverpool, CCB, indirect buffers, draw/dispatch, releases/waits e barreiras de consumidores (L12/L13) | GNM atual tem allowlist e transferências limitadas. Preservar validação bounded/sem efeitos em lote inválido; não importar skips de comandos | P1 + HLE; alta |
| P10 — compute/LDS | Dispatch, data_share, atomics e passes de shared memory (L06/L13) | Não basta aceitar DS/opcodes: criar dispatch GNM, workgroup/LDS e barreiras reais com readback | P3/P4/P9; conforme workload |
| P11 — caches/coerência | Publication snapshots, journals, leases/ticks, staging e pipeline cache (L09..L12/L15/L17) | Snapshots PS4 ainda por draw e alias de alvo recusado. Introduzir identidade e geração antes de reaproveitar ou remover recusas | P1/P9; alta antes de caches persistentes |
| P12 — VideoOut | HLE de buffers/flip/vblank, presenter, swapchain e contrato de frame (L16) | Perfil diagnóstico grava framebuffer; falta apresentação Gen4. Adaptar à apresentação existente, incluindo D3D12/UWP | P1/P9 + HLE; alta para primeira imagem na aplicação |
| P13 — runtime/app | Ligação submit→GPU→flip e observabilidade dos estágios (L13/L16) | Reutilizar o runtime único Kyty; não copiar processos Android/FEX, driver loader ou dashboard ausente | P11/P12 no caso geral; primeira versão pode ser síncrona e limitada |
| P14 — regressão | Tests de bindings, publication, cópias, barreiras, primitives e probes numéricos/readback (T4 e LEG) | Recriar fixtures próprias para GCN→IR→SPIR-V→DXIL e resultados nos três hosts; comparar com regressões PS5 | Cada incremento; imediata |
| P15 — GS/tess/mesh | Copy/fetch/tessellation e emulação compute/Metal (L05/L18) | Referência de semântica/fallback, não requisito imediato do perfil. Distinguir capacidade PS4 de mecanismo PS5 NGG/mesh | P3/P4/P9/P10; posterior, por título |

Esse mapa cobre os eixos da fase 4, não afirma suporte completo de todas as famílias ou títulos em qualquer uma das bases.

## 4. Achados que exigem cuidado concreto

### Samplers e formatos

`sampler.cpp` limita anisotropia/LOD conforme o host e usa `.unnormalizedCoordinates=false`, com tratamento de coordenadas não normalizadas no shader. Copiar apenas vk::SamplerCreateInfo perde esse contrato. Para o Kyty, começar com **nearest + clamp-to-edge + IMAGE_SAMPLE_L/LOD0**, descriptor traduzido semanticamente e sampler/bindings separados por estágio; só depois comparação, border, anisotropia e coordenadas não normalizadas.

`IsImageBindingValid` consulta `is_mapped(address,1)`: é evidência de admissibilidade inicial, **não de validação do payload completo**. Manter a validação de pitch*tamanho/range do Kyty e não reduzir os guards atuais a uma sondagem de um byte. Não adotar fallback de formato inventado; image_binding_contract_test.cpp verifica pares data/number format contra a tabela existente.

### Publicação, detiling e reutilização

`GuestPublicationSnapshots` explicita que seus bytes privados não são prova de mapeamento/permissão e que a execução guest deve estar excluída durante a publicação. Falha de backing write não deve avançar o baseline; unmap/remap muda identidade. Isso é útil para futuros caches PS4, mas **não substitui a lease/mapeamento do kernel Kyty**.

`GetImageUploadSourceBarrier` identifica o consumidor real: detiler lê o buffer tiled como shader read/compute, enquanto a cópia posterior lê outro scratch linear como transfer read. Transpor a dependência para resource states/UAV barriers D3D12, e não copiar masks Vulkan literalmente.

`TileManager::RefreshScratchCompletions:148` distingue trabalho gravando/submetido/concluído; o tick da command list aberta não prova conclusão. `buffer_cache.cpp:983` registra o risco de forçar Flush no meio do upload/present. Preservar fence efetiva e geração antes de reciclar; atraso em frames/ticks não é substituto universal de conclusão. O caminho legado UploadStagingArena separa bookkeeping de alocação nativa e só permite Complete com fence/discard comprovado. Os limites legados 4MiB/64MiB/16 slabs são parâmetros daquela implementação, não valores a copiar para Xbox sem medir.

`BufferConsumerBarrierEnabled` está ligado por padrão salvo valor exato 0; `BufferShaderAccessBarrierEnabled` é experimento desligado por padrão salvo valor exato 1. Não tratar ambos como política aprovada ou ativá-los indistintamente. Escrita indicada no descriptor/ledger também não prova que uma escrita GPU ocorreu.

### Não importar comportamento permissivo como correção

Há UNREACHABLE e caminhos sem implementação em gnmdriver.cpp, fetch_shader.cpp e vector_memory.cpp. Em liverpool.cpp há skips para WAIT_REG_MEM com endereço nulo e indirect buffers inválidos. A existência de um opcode/export no switch não comprova semântica completa. O perfil Kyty deve continuar recusando o que não suporta com diagnóstico e sem publicação parcial; implementar novos waits/indiretos com budgets, endereços validados e testes de ordenação.

A referência amplia VOPC/EXEC e interpolação, mas não certifica todos os casos NaN, denormal, arredondamento, EXEC vazio ou modos 64 bits. Probes como GraphicsDx10ClampNanCompute, GraphicsVMulLegacyF32Compute e GraphicsVRsqClampF32VulkanProbe sugerem casos de teste. Não transportar valores numéricos de opcode Gen5 para o frontend GCN.

### Mobile/Metal não é UWP/D3D12

Os caminhos gráficos examinados usam Vulkan e/ou um adaptador Metal, não um backend D3D12 pronto para o Kyty. Há políticas Android específicas: FrameLedger::Enabled retorna false fora do Android; vk_swapchain_policy trata Suboptimal de forma diferente no Android; algumas fixtures incluem unistd.h; o embedding FEX restringe host/ABI. Esses detalhes devem virar contratos portáveis/fixtures, não dependências de plataforma no runtime UWP.

MetalSoftwareTessellatorSource e a receita Turnip mesh-via-compute são referências posteriores de algoritmos e capacidades, não importação de runtime/driver. O Kyty já dispõe de HostShaderFeatures, sampler caches, IR/resource passes, caches de pipeline e tilers nos backends existentes. Comparar primeiro e manter PS5 funcional; não sobrescrever esses componentes com uma base anterior.

## 5. Roteiro recomendado e critérios de pronto

1. **Sampler PS4 + IMAGE_SAMPLE_L**: extrair campos de Sampler/LOD como referência, declarar subconjunto nearest/clamp, reservar bindings sem sobrepor buffers58/60 e imagens64/65 e ligar à IR existente. Fixture 2D com cores por texel, bordas fora de [0,1], orientação/pitch, updates e uso VS/PS; validar SPIR-V, DXIL, D3D12/WARP/Vulkan e ELF GNM. Sem depender de custom border ou extensões opcionais.
2. **Caminho curto de apresentação**: VideoOut Gen4 linear/RGBA8, registro de buffers/flip/vblank e first-present na aplicação, reutilizando a apresentação Kyty e mantendo o diagnóstico headless. Registrar primeiro frame, estado e erro bloqueante; não esperar todos os formatos/compute para apresentar a fixture suportada.
3. **Frente de shaders gerais**: VOPC/EXEC e branches por máscara, typed fetch/MTBUF e índices. Cada família exige definição por lane, CFG e um resultado GPU verificável; interpretar recursos indiretos só depois de ter identidade/bounds.
4. **Layouts e persistência**: primeiro layout tiled GCN e sizing/CPU oracle; depois produtor→detiler→imagem com barriers; depois staging/cache com geração e fence, dirty/publication e alias read-after-write. Só remover a recusa de draw-target como recurso quando a cadeia completa estiver testada.
5. **Expansões por workload**: depth/MSAA/MRT, compute/LDS/atomics, GS/tess e formatos avançados. Usar os contratos e probes Lumen como checklist, não como certificação automática.

Pronto por incremento continua sendo código integrado Kyty + fixture legal + build/testes e readback nos hosts disponíveis + regressão PS5 + UWP compilável + documentação. O roteiro não foi implementado por esta auditoria.

## 6. Testes da referência e nível real de evidência

| Conjunto | O que aproveitar | Limite |
|---|---|---|
| T4/image_binding_contract_test.cpp, sharp_fetch_contract_test.cpp, resource_tracking_contract_test.cpp, srt_flatten_contract_test.cpp | Guards, formatos, offsets e propagação de descriptors | Código de testes lido; não executado aqui; não prova renderização final |
| T4/guest_publication_snapshots_test.cpp, guest_publication_inspection_test.cpp, buffer_consumer_barrier_test.cpp, buffer_copy_resolve_test.cpp | Mudanças CPU, backing, ordenação e falhas sem efeitos | Exige mapear sincronização do kernel/hosts Kyty |
| T4/detile_source_contract_test.cpp, primitive_expansion_contract_test.cpp, rect_list_contract_test.cpp | Resultado GPU/layout, índices, overflow e topologia | Algumas fixtures são dependentes do host; requer novos builds/fixtures locais |
| T4/frame_ledger_test.cpp, bounded_draw_diagnostics_test.cpp, swapchain_policy_test.cpp | Observabilidade limitada, classificação de comandos e apresentação | Seletores Android/ambiente e política de host não são requisitos Xbox |
| source/ probes legados GraphicsUploadStagingVulkanProbe.cpp, GraphicsStorageBaseMipOneCompute.cpp, GraphicsDepthStencilCopyVulkanProbe.cpp e probes de ALU | In-flight lifetime, mip base, depth/cópia e precisão | Confirmar árvore selecionada; um probe isolado não valida GNM/runtime completo |
| source/gen4/CMakeLists.txt e source/CMakeLists.txt | Targets/registro de testes e probes opt-in | Há EXCLUDE_FROM_ALL e opções default-OFF; registro em CTest não significa binário construído ou teste aprovado |

As tabelas de jogos/imagens/FPS do README são declarações da referência, não evidência reproduzida neste audit ou compatibilidade transferível para Kyty/Xbox. O baseline local Kyty (25 testes e readbacks da entrega anterior) foi conferido em documentos/código e `build/stage4-textures/evidence-summary.log`; **não foi reexecutado nesta tarefa documental**.

## 7. Licenças e fronteira de reutilização

Conforme os avisos locais: LICENSE contém GPLv2; LICENSES.md declara integração Lumen/Gen5 GPL-2.0-only, Gen4 derivado GPL-2.0-or-later com seleção v2, herança Kyty MIT e exceções por arquivo. `guest_publication_snapshots.h` declara MIT; vários contratos/renderers Gen4 declaram GPL-2.0-or-later; os headers legados sem SPDX exigem rastrear origem/aviso aplicável antes de copiar. Não classificar toda a árvore como MIT ou toda mudança como autoria Lumen.

O próprio LICENSES.md lista questões de redistribuição envolvendo dependências SPIRV-Tools/Apache-2.0, FDK AAC e receitas de driver com GPL-3.0. Isso é registro das declarações locais, **não aprovação jurídica de combinação/distribuição**, nem conclusão de que todo arquivo individual esteja impedido de servir como referência. Preservar notices, proveniência e termos do arquivo/dependências efetivamente usados; não importar APKs, drivers ou fechar licenciamento só com um aviso no relatório. Nenhum código externo foi copiado nesta tarefa.

## 8. Verificação desta auditoria

Executados apenas inventários rg, leituras Get-Content, parsing de manifests e comparação/hashing de arquivos. Uma busca inicial com brace expansion não aceita por PowerShell e dois caminhos presumidos inexistentes foram corrigidos pela enumeração real; não foram usados como evidência. Nenhum build/teste Lumen, instalação, execução de jogo ou alteração externa. Nenhum código/teste/configuração de build Kyty foi modificado.

Artefatos locais: `build/lumen-audit/snapshot.json` (742 arquivos/hashes e comparação engine), `verification.json` (releitura/fingerprint e referência inalterada), e validação de links/documentos. Esses arquivos de build são evidência local; o resumo reproduzível e as referências principais permanecem neste documento. O snapshot não é um hash do repositório inteiro nem substitui um commit Git.

**Resultado:** contribuição arquitetural e roteiro mapeados para todas as partes P0..P15; auditoria documental concluída. Fase 4, shaders gerais, apresentação e compatibilidade comercial/Xbox continuam incompletos. Próxima implementação recomendada permanece sampler nearest/clamp + IMAGE_SAMPLE_L no contrato existente.
