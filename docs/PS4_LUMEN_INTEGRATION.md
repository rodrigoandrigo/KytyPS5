# Integração Lumen na fase 4 — checkpoint de implementação

Validação liberada pelo usuário em 2026-11-08: o adiamento de testes registrado abaixo foi revogado. Reconstrução e testes dos perfis limitados concluídos; resultados novos em `PROGRESS.md` e `build/lumen-validation`. P0–P15 continua incompleto.

Estado em 2026-11-08: **INCOMPLETO; perfis limitados validados nesta rodada**. A solicitação abrange todas as contribuições da [auditoria](PS4_LUMEN_GPU_AUDIT.md), incluindo P0..P15. **Critério confirmado pelo usuário:** todo o suporte GPU disponível no Gen4 do Lumen, não apenas contratos limitados de P0..P15. Isso inclui todos os estágios/ISA implementados, pipelines, buffers/imagens, formatos/layouts, compute/LDS/atomics, PM4, coerência e apresentação existentes nessa árvore. Adaptações mobile/Metal/Gen5 são referências auxiliares, não substitutos desse critério. Manifesto de escopo em build/lumen-integration/gen4-scope.json.

A implementação foi retomada com tabelas escalares, IB gráfico, flat/default PARAM e um perfil de MTBUF/fetch externo/draw indexado; não integrou todas as partes. O adiamento anterior foi revogado pelo usuário. Resultados atuais de testes e limites constam na seção final; aprovações históricas não substituem essa evidência.

## Alterações no código

| Área | Adaptação escrita | Arquivos |
|---|---|---|
| SMRD | S_BUFFER_LOAD_DWORD/X2/X4/X8/X16, VS e PS, descriptors diretos s0..3 ou s4..7, offset imediato em DWORD convertido para bytes; bounds pela largura completa; definição e RSRC1 pelo grupo inteiro | GcnDecoder.cpp, GcnCompiler.cpp, ps4Gnm.cpp |
| Sampling | IMAGE_SAMPLE_L normalizado para a IR existente; T# RGBA8 linear em s8..15, S# nearest/clamp-last-texel em s4..7; coordenadas normalizadas, min/maxLOD0, sem comparação/bias/border/aniso | GcnDecoder.cpp, GcnCompiler.cpp |
| Bindings host | Sampler PS66/VS67, estático D3D12 e imutável Vulkan; imagens64/65 e buffers58/60 preservados | d3d12/ps4RasterProbe.cpp, renderer/ps4TransferProbe.cpp |
| ALU | VOP1 adicionais: CVT_U32_F32, CVT_I32_F32, RNDNE, EXP, LOG, RSQ, SIN, COS, NOT, BFREV; VOP2 adicionais: min/max I32/U32, shifts lógicos/aritméticos normais/reversos | GcnDecoder.cpp, tradução/IR existentes reutilizadas |
| Diagnóstico GNM | Até256 draws e256 submissões de metadata; tentativa/ordinal/DCB/PC/endereço real do stream/target/tamanhos, fase Validation/Host/Publication/Completed e erro limitado1024bytes; flags separadas de host completion, publicação guest e rejeição | ps4Gnm.h/.cpp |
| Tabelas escalares | Preâmbulo S_LOAD nas cinco larguras, leitura completa por callback runtime, até64leituras/4KiB por estágio; grupos internos até104SGPRs; descritores em slots carregados; código normalizado usado por RSRC1 | GcnCompiler.h/.cpp, GcnDecoder.cpp, ps4Gnm.cpp |
| PM4 indireto | INDIRECT_BUFFER gráfico incondicional VMID0, chamada/chain, até256IBs/profundidade8, orçamento de bytes conjunto4MiB e detecção de ciclos; alias de qualquer output com comando/shader/tabela recusado antes de executar | ps4Gnm.h/.cpp |
| Memória vetorial | MTBUF X/XY/XYZ/XYZW FLOAT32 de largura correspondente, IDXEN e offset DWORD; normalização de representação para IR raw; MUBUF DWORDX2/X3 e guards antes de multiplicar stride | GcnDecoder.cpp, GcnCompiler.cpp, ps4Gnm.cpp |
| Fetch externo | S_SWAPPC inicial VS, S_LOAD de V# seguido de MUBUF/MTBUF e waits, retorno S_SETPC correspondente; função até1024DWORDs, confirmação do intervalo inteiro, sem observar PC de retorno | GcnCompiler.h/.cpp, GcnDecoder.h/.cpp, ps4Gnm.cpp |
| Draw indexado | INDEX_TYPE16/32 e DRAW_INDEX_2, snapshots/bounds/alias, builder sceGnmDrawIndex; DrawIndexed nos adaptadores existentes, sem endian swap/offset/base vertex/instancing | ps4Gnm.h/.cpp, ps4Raster.h, d3d12/ps4RasterProbe.cpp, renderer/ps4TransferProbe.cpp |
| VCC/EXEC |16comparações F32 VOPC, CNDMASK, S_AND_SAVEEXEC_B64 com VCC e restauração S_MOV_B64 para EXEC; pilha até8níveis, definição por caminho e preservação de VGPRs inativos | GcnDecoder.h/.cpp, GcnCompiler.cpp, ps4Gnm.cpp |
| PARAM flat/default | V_INTERP_MOV modoP0 nativo flat com primeiro provoking vertex; quatro defaults RGBA constantes, validação comum de SPI_PS_INPUT_CNTL | GcnDecoder.h/.cpp, GcnCompiler.h/.cpp, ps4Gnm.cpp |
| Fixtures | Sampled VS/PS com buffer de modulação; casos positivos/negativos SMRD, ALU, sampler e histórico; variante linked DXIL; três cenas sampled com oracle CPU | GcnDecoder.h/.cpp, Ps4GcnShaderTests.cpp, Ps4GcnDxilTests.cpp, Ps4GnmTests.cpp, Ps4RasterTests.cpp, run-ps4-gcn-gpu.ps1 |

O perfil sampled direto ainda ocupa os16 user SGPRs com um buffer + sampler + imagem. O novo perfil de tabelas recebe apenas s0..1 como ponteiro e carrega dois V#, um T# e um S# em SGPRs internos: dois buffers + uma imagem + sampler por estágio, preservando a ABI de16 user SGPRs. Os dados de buffer/imagem continuam recursos GPU reais nos adaptadores existentes; somente o preâmbulo escalar estático é especializado em constantes. S_LOAD no corpo/branches, offsets dinâmicos, tabelas mutáveis durante o draw e múltiplas imagens distintas por estágio não foram implementados. Samplers gerais, mips e filtragem linear permanecem pendentes.

SubmissionEvidence cobre também rejeições anteriores a PrepareDraw, distingue a fase de falha e mantém o erro limitado. Não captura payloads. DrawEvidence contém apenas metadata e retorna uma cópia sob mutex. host_completed significa sucesso do contrato Render, inclusive em recording renderer de teste; não identifica GPU real. guest_published só é marcado depois da escrita do alvo. Uma falha tardia pode ter linhas publicadas e batch_rejected simultaneamente: diagnóstico não transforma várias escritas em commit atômico. Não usar esse histórico como fence, prova de permissões, journal de dirty pages ou evidência de escrita efetiva de cada invocation.

## Estado por parte e ponto de retomada

| Parte | Estado desta integração | Próxima alteração concreta / dependência |
|---|---|---|
| P0 | Escopo Gen4 confirmado; históricos por draw/submissão validados no perfil limitado | Completar inventário semântico de cobertura e fixtures de cada capacidade Gen4; hooks de apresentação ainda pendentes |
| P1 | Tabelas iniciais imutáveis e slots internos validados no perfil limitado | Identidade de backing/generation, arrays/SRT dinâmicos e listas gerais de recursos |
| P2 | Cinco larguras S_BUFFER_LOAD e S_LOAD inicial validadas no perfil limitado | S_LOAD em fluxo geral, offsets dinâmicos e tabelas mutáveis; bounds em IR e SRT |
| P3 | MTBUF FLOAT32, fetch externo inicial e índices16/32 validados no perfil limitado | Formatos packed/normalized, fetch com ALU/loads intercalados, index offsets/base vertex, instancing e draws indiretos gerais |
| P4 | VOPC F32/I32/U32, CMPX condicionado a frame salvo, CNDMASK, EXEC balanceado, else XOR e guard EXECZ validados no perfil limitado | Comparações 64bits, operações gerais de máscaras, branches VCC/EXEC gerais e loops; precisão/NaN/denormais ainda sem evidência |
| P5 | Flat MOV P0 e quatro defaults validados no perfil limitado | P1 intermediário real, MOV P10/P20, sample/centroid e último provoking vertex |
| P6 | Pendente | Layout GCN tiled e sizing por subrecurso com oracle CPU; não reutilizar layout Prospero por cast |
| P7 | Sampling restrito + dois buffers por tabelas escritos | Ampliar samplers, imagens/formatos/LOD gerais e storage, preservando limites |
| P8 | Pendente | Depth/stencil, MRT/MSAA, exports/discard e topologias |
| P9 | IB gráfico incondicional/call/chain validados no perfil limitado | Waits/releases, IB condicional/CCB/compute e ordenação; não incorporar skips permissivos Lumen |
| P10 | Pendente | Dispatch GNM, workgroups, LDS/atomics e barriers com efeitos reais |
| P11 | Pendente | Publication journal/backing/generation, staging protegido por fence e alias read-after-write |
| P12 | Pendente | VideoOut Gen4 linear/RGBA8, registro/flip/vblank ligado à apresentação Kyty |
| P13 | Pendente | submit→GPU→flip no runtime único e primeira imagem na aplicação |
| P14 | 10/10 testes e270 readbacks aprovados no subconjunto | Completar fixtures das demais partes e ELF específicos dos novos opcodes; ampliar cobertura numérica |
| P15 | Pendente | GS/tess/fallbacks por workload, após dispatch e ABI/lanes/recursos; sem importar Metal/driver como backend UWP |

**Retomada exata:** continuar todo o suporte Gen4 confirmado, usando o manifesto e a tabela acima. A primeira indireção estática já está escrita; o perfil inicial de vertex fetch externo/MTBUF/índices já está escrito. O primeiro perfil EXEC/VCC balanceado também está escrito. O próximo bloqueio é ampliar fluxo por máscara e comparações/ops de máscara gerais, seguido por layouts GCN e formatos/fetch/instancing gerais. Depois integrar dispatch/coerência e VideoOut ao runtime/apresentação existente. Não confundir preâmbulo escalar especializado com acesso GPU geral a tabelas mutáveis. O usuário já liberou testes; validar cada novo incremento e preservar os resultados desta rodada.

## Histórico do adiamento e comandos de referência

Antes da liberação desta rodada, nenhum build, CTest, validador SPIR-V/DXIL ou probe GPU havia sido executado nesta integração. As mensagens PASS nos testes são expectativas programadas. Esse era o estado histórico; os logs novos estão registrados na seção final.

Após terminar a integração global solicitada, recompilar os targets ps4_gcn_shader_tests, ps4_gnm_tests e as regressões compartilhadas no build desktop existente; gerar fixtures em uma pasta nova; executar o runner tests/run-ps4-gcn-gpu.ps1 com o tradutor Mesa-UWP local já disponível em build/stage3/spirv_to_dxil-dist e SDK instalado. As variantes DXIL17/18 usam gcn-sample e gcn-table-sample, respectivamente;19..23 cobrem flat e os quatro defaults. Raster59..64 verifica sampling direto/por tabela, quatro buffers/duas imagens nos perfis por tabela, UV fora de[0,1], updates e pitch256. Raster65..69 confere primeiro provoking vertex e defaults. A variante linked DXIL24 e raster70/71 exercitam o perfil de fetch externo; raster72/73 usa índices16/32 para selecionar o triângulo oposto ao draw-auto. Cada cena tem2048pixels no oracle CPU preparado. Em seguida executar CTest, ELF GNM, build UWP existente e auditoria de imports. Não instalar/deploy. Os comandos de build históricos estão em PROGRESS.md e build/stage4-textures; adaptá-los ao estado final, não tratar binários antigos como build deste código.

Ainda faltam fixtures ELF guest específicas de IMAGE_SAMPLE_L, fetch externo e draw indexado; os casos sampled atuais preparados cobrem compilador, adaptadores e sessão GNM com recording renderer, não execução AMD64 desse opcode no runtime. As novas ALU têm testes de emissão preparados, mas faltam readbacks numéricos específicos. shader_cfg PS5 passou nesta rodada; status UWP está registrado na seção final. Nenhuma execução Xbox/AppContainer/gameplay é comprovada.

## Referências e proveniência

Continuação também consultou scalar_memory.cpp, regs_shader.h, regs_primitive.h e PM4CmdIndirectBuffer/ProcessGraphics da árvore Gen4; mantidos como referência somente leitura. Implementações próprias adaptadas a partir da semântica observada em source/gen4/src/shader_recompiler/frontend/translate/scalar_memory.cpp, opcodes.h, vector_memory.cpp; video_core/amdgpu/resource.h; texture_cache/sampler.cpp; renderer_vulkan/frame_ledger.h e bounded_draw_diagnostics.h. Reutilizados os tradutores, IR, especialização e emissores Kyty. Não houve cópia literal de arquivos Lumen, dependência nova, instalação, mudança no checkout Lumen ou substituição dos caminhos PS5/D3D12/Vulkan. As ressalvas de licença da auditoria permanecem aplicáveis a futuras importações de código.

## Limites do perfil P3 escrito nesta continuação

A normalização MTBUF aceita somente FLOAT32 em formato32/32x2/32x3/32x4 correspondente ao número de componentes. Não é conversão genérica de DFMT/NFMT; packed, UNORM/SNORM, stores e controles de cache/ADDR64/OFFEN permanecem recusados. Não há evidência nova de comportamento de NaN/denormais. Os loads de2/3DWORDs recebem o mesmo clamp de índice anterior à multiplicação de stride.

Fetch externo precisa ser uma chamada inicial única, após um eventual preâmbulo escalar. Não segue PC arbitrário, não executa código host e não introduz outro recompiler. SGPRs de retorno ficam desconhecidos para o corpo do VS; código que os observa é recusado. Código da função e tabelas são snapshots lidos pela lease do runtime, não cache persistente nem garantia de exclusão de todas as threads guest. RSRC1 considera também ponteiro/link e destinos de S_LOAD, mesmo quando a IR elimina esses valores. Interleaving de S_LOAD e fetch vetorial continua fora do perfil.

Índices usam buffer GPU real nos dois adaptadores e permanecem vivos até o fence/readback existente. Tipo e tamanho são explícitos, alinhamento é2/4bytes, offsets/base vertex ficam zero, uma instância e triangle-list. Não há primitive restart, endian swap, INDEX_BASE/DRAW_INDEX_OFFSET_2 nem arrays indiretos de draws. O builder HLE foi adicionado, mas não foi exercitado por ELF guest nesta execução. Nenhuma dessas alterações foi compilada ou testada; aprovações históricas não se aplicam a elas.

## Perfil P4 de VCC/EXEC escrito, ainda não validado

O frontend GCN aceita as16comparações VOPC F32 (incluindo ordered/unordered) e V_CNDMASK_B32 com VCC explicitamente definido em todos os caminhos. Não são apenas opcodes reconhecidos: a análise de fluxo registra definição de VCC por interseção e a tradução usa as comparações e a seleção existentes. Sem comprovação numérica de NaN/denormais neste ciclo.

S_AND_SAVEEXEC_B64 só aceita origem VCC e par SGPR alinhado; S_MOV_B64 só restaura EXEC pelo par salvo no topo da pilha. A IR existente predica WriteOperand por EXEC e gateia WriteVectorMask, preservando o valor anterior dos VGPRs inativos. O frontend exige que todos os componentes de um destino mascarado já estejam definidos, protege ambas as palavras dos pares salvos, compara pilhas nos joins e limita nesting a8. Descriptors imutáveis e RSRC1 também verificam as duas palavras.

Branches comuns dentro da região, exports ou END sem restore, reuso do par salvo, valores indefinidos e mismatches são recusados. A continuação abaixo acrescenta apenas else XOR e guard EXECZ estruturados; não há MOV arbitrário para EXEC, CMPX, operações B64 gerais ou branches VCCZ/EXEC gerais nesse perfil. Isso não fecha divergência GCN geral. Exports ocorrem somente após restaurar a máscara original; não há discard parcial anunciado.

Preparados, sem executar: três módulos de máscara simples/aninhada/seleção; emissão dos16predicados; negativos de VCC indefinido, SGPR salvo sobrescrito, restore incorreto, destino VGPR sem valor para lane inativa, branch, merge com pilhas diferentes, nesting e export sem restore. Linked DXIL25..27 e raster74..76 verificariam todos os2048pixels com oracle CPU para lanes alteradas e preservadas. GNM recording renderer cobre os três programas e recusa alocação insuficiente de SGPRs. Não há ELF AMD64 específico desses opcodes ainda, nem resultado de shader validator, GPU ou regressão nesta execução.

## Continuação P4: complemento de EXEC e guard estruturado

Escrito o padrão S_XOR_B64 EXEC,saved_pair,EXEC para trocar then por else sem ativar lanes fora da máscara salva. O par precisa ser o topo vigente, intacto, e só pode ser complementado uma vez. A fase then/else integra o estado de fluxo e precisa coincidir nos joins. A proteção de ambas as palavras salvas continua mesmo após o complemento. Restauração remove o frame e recupera o EXEC anterior.

S_CBRANCH_EXECZ só aceita salto forward para o else ou restore do mesmo frame. Não vira branch por pixel: o caminho compartilhado ConditionRef emite voto de subgrupo e combina metades quando aplicável. Continua proibido saltar para uma VALU arbitrária, atravessar frames, fazer loops ou executar exports sob máscara reduzida. As restrições de definição de VGPRs e descriptors não foram removidas.

Cinco fixtures próprias preparadas: guarded/sem guard, then vazio, else vazio e nested. Oracle CPU para2048pixels por cena em raster77..81; linked DXIL28..32; casos GNM com recording renderer e RSRC1. Negativos cobrem par errado, segunda inversão, guard para destino incorreto e frame pai. Nenhum desses casos foi compilado, validado ou executado. Também permanecem sem validação os incrementos anteriores. Falta ELF AMD64 específico desse caminho, comparações inteiras/64bits/CMPX, fluxo geral VCC/EXEC e loops.

Próxima implementação: comparações inteiras e extensão das máscaras sob contrato explícito, preservando reconvergência e lanes inativas; depois avançar os layouts GCN. P0–P15/Gen4 completo continua INCOMPLETO. Testes permanecem adiados por instrução do usuário.

## Continuação P4: comparação inteira e CMPX (não validada)

O decoder próprio aceita os oito predicados I32 e U32 e CMPX F32/I32/U32. CMPX é normalizado à comparação base com uma marca exclusiva GCN; o tradutor escreve VCC e depois EXEC, ambos limitados pelo EXEC de entrada, sem alterar SCC. A marca nasce falsa no decoder compartilhado: preservar o caminho PS5 é intenção de implementação, ainda sem regressão executada. A mudança no layout de Instruction exige reconstruir todos os dependentes quando os testes forem liberados.

CMPX exige um frame EXEC salvo ativo. Não cria frame, não permite export parcial nem restauração arbitrária. Continua sujeito aos pares protegidos, destinos previamente definidos por lane, reconvergência, else e guards existentes. Comparações de 64 bits, máscaras B64 gerais, branches VCC/EXEC gerais e loops continuam pendentes. As ausências de CMPX/I32/U32 descritas nas seções anteriores são históricas; este checkpoint é o estado atual escrito.

Preparadas sete fixtures: I32/U32 com valores que cruzam o bit de sinal; CMPX F32 LT/F/T e I32/U32 LT sob máscara externa, else e restore. A seleção de azul após restore depende de VCC, distinguindo a atualização conjunta de VCC+EXEC. Cor inicial fixa cobre lanes fora da máscara externa. Fontes de testes incluem os oito predicados inteiros, recusa CMPX sem frame, marca default falsa, GNM/RSRC1, linked DXIL33..39 e raster82..88 com oracle de2048pixels por cena. Nenhum desses testes foi executado; os oracles também aguardam validação.

Retomada: ampliar comparações de64bits e fluxo/máscaras gerais a partir dos tradutores existentes, mantendo definições por lane e reconvergência; depois layouts/formatos GCN. P0–P15 e suporte Gen4 completo permanecem INCOMPLETOS. Não houve compilação, validator, CTest, GPU, UWP ou Xbox nesta continuação; a instrução de testar apenas após integrar todo o escopo continua vigente.

## Validação executada em 2026-11-08 — perfis integrados

O usuário liberou os testes nesta rodada. A suíte de shaders e os testes GNM passaram após corrigir duas instruções MOV mal codificadas nas fixtures SMRD. A função CMake dos testes completos agora liga onecore também em MinGW, resolvendo MapViewOfFile3/UnmapViewOfFile2 na regressão PS5. Diagnóstico de falhas de shader inclui a linha da chamada.

Evidências em `build/lumen-validation`: `shaders.log` (saída0), `gnm-unit.log` (saída0), `ctest.log` (10/10: oito casos GNM, shader GCN e shader_cfg PS5), `gpu.log` e `results.json`. O runner GPU compilou os probes atuais e validou80 módulos DXIL em40 pares. Cada backend executou89 variantes de raster mais uma cena mista:270 readbacks de2048pixels, em D3D12 Radeon RX6600M, WARP e Vulkan Radeon RX6600M. Zero erros reportados nas camadas D3D12 e Vulkan. Inclui sampling direto/por tabelas, flat/default, fetch externo/índices, máscaras/else/guards e I32/U32/CMPX, dentro dos contratos limitados deste documento.

A reconstrução dos dependentes foi executada nesta rodada. O Ninja perdeu registros e iniciou uma repetição após regeneração; a repetição foi interrompida. `commands.json` e `relink-current.py` registram comandos CMake atuais usados para recompilar o teste corrigido e ligar os objetos próprios recém-reconstruídos, sem mistura de objetos antigos de Instruction. O build inicial e as falhas estão preservados em `build-desktop.log`; compile/link têm logs separados. Isso é evidência de compilação e execução dos targets indicados, sem alegar que toda a aplicação desktop foi construída nesta rodada.

Estes resultados substituem o estado não validado dos perfis acima, mas não fecham P0–P15. Não exercitam jogo comercial, lançamento normal PS4, apresentação integrada ou Xbox. A regressão PS5 é shader_cfg, não gameplay. Próximo incremento de ISA: comparações GCN de64bits com verificação de ambos os registradores de cada par, limites RSRC1 e resultados de lanes; depois fluxo/máscaras e layouts gerais. Build completo UWP/D3D12 aprovado via `src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp` (uwp.log); auditoria explícita Test-Imports retornouTrue para4binários (uwp-imports.log). Sem instalação/deploy ou execução AppContainer/Xbox.
