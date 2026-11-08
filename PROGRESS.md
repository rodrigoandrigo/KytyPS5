# Progresso da missão PS4 no KytyPS5

> Estado inicial baseado exclusivamente em CODEX_PS4_COMPATIBILITY_GOAL.md; verificar no checkout antes de confiar.

## Objetivo
Implementar progressivamente compatibilidade PS4 nativa no KytyPS5, preservando PS5, UWP/D3D12 e Vulkan.

## Histórico informado
- [x] Etapa 0: auditoria documentada (conforme arquivo de objetivo).
- [x] Etapa 1: detecção e seleção PS4/PS5 (conforme arquivo de objetivo).
- [x] Etapa 2: loader PS4 em fixtures ELF/SELF (não boot comercial).
- [x] Etapa 3: HLE diagnóstico Windows (não Xbox executado).
- [ ] Etapa 4: GPU completa e integração runtime/apresentação.
- [ ] Etapa 5: integração e execução UWP/Xbox comprovadas.
- [ ] Etapa 6: matriz de compatibilidade e regressões ampliadas.

## Sessão — 2026-11-08: shaders gerais, incremento de interpolação e ALU

- Implementado: exports PARAM0..3 completos no VS, validação de exports por caminho, VINTRP P1/P2 com pares adjacentes, perspectiva central e interface explícita no GcnCompiler. Registros GNM SPI_* conferem contagem, enable/address e mapeamento identity/smooth.
- Implementado: VOP3 F32 add/sub/subrev/mul/min/max/MAD/FMA, abs/neg, omod e clamp; min/max VOP2. Encodings GCN separados de RDNA2, literais VOP3 e controles fora do contrato recusados.
- Evidência já obtida: 12 fixtures SPIR-V validadas (mais dois checks de interface com quatro parâmetros); 18 objetos DXIL validados; dez cenas x três renderizadores (D3D12 Radeon RX 6600M, WARP e Vulkan) aprovadas com zero erros debug/validation. Logs em build/stage4-shaders/shaders-final.log e gpu-final.log.
- Runtime guest: ELF AMD64 próprio usou imports GNM e conferiu todos os 2048 pixels com perspectiva e com ALU, em duas submissões válidas e uma inválida. Logs gnm-interpolation.log e gnm-alu.log.
- Build completo UWP/D3D12 aprovado: build/stage4-shaders/build-uwp-final.log. Execução UWP/Xbox não realizada. Nada instalado.
- Validação final concluída deste incremento: build desktop do núcleo completo (`cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_raster_vulkan_tests -j 4`) aprovado; testes completos relinkados com os mesmos objetos de produção finais e comandos gerados pelo CMake. Suíte CTest selecionada: **21/21 aprovados**, incluindo `shader_cfg`, proveniência escalar, recursos PS5, detecção/loader/CPU/HLE e GNM. Logs: `build-desktop-final.log`, `relink-final.log`, `ctest-final.log` e `ctest-details.log`.
- Validação adicional: quatro slots exportados com apenas PARAM3 consumido e cores distintas aprovados na GPU. Rejeições de contagem/enable/mapa GNM, RSRC1 insuficiente e pares P1/P2 quebrados não executam o renderer nem alteram estado/framebuffer. A volta a um lote válido passa.
- Comparação RGB de readbacks: D3D12 Radeon e Vulkan idênticos nas dez cenas. WARP difere no máximo um nível UNorm nos gradientes; cada backend também passou contra a referência independente para todos os canais RGBA. Registro: `readback-comparison.log`.
- Auditoria final dos imports UWP contra WindowsApp.lib aprovada (`audit-uwp-final.log`). Build não comprova execução AppContainer/Xbox.
- Incremento de interpolação/ALU validado; **missão de shaders gerais e etapa 4 continuam parciais**.
- Limites: sem buffers/fetch/texturas, EXEC/VCC divergente, loops, interface genérica de interpolação, tiling geral e video-out. Etapa 4 permanece aberta.
- Verificações usadas: geração das fixtures por `ps4_gcn_shader_tests`, `tests/run-ps4-gcn-gpu.ps1`, `ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'`, `src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp` e auditoria de imports. Nenhum deploy/registro/instalação.
- Falhas finais: nenhuma nas verificações acima. Há avisos preexistentes de `std::wstring_convert` no build GCC; não impediram compilação/testes.
- Próximo incremento da missão: `s_buffer_load_dwordx4` com descritor PS4 nos user SGPRs, validação de base/limites e buffer real ligado como recurso Vulkan/D3D12. Exercitar mudança de dados entre submissões no ELF guest; reutilizar `Translator::S_LOAD`/`ReadConstBuffer` e o rastreamento de recursos existentes em vez de criar uma segunda IR.

## Análise das pendências da fase 4 — 2026-11-08

- Pedido atual: análise e relatório; nenhuma implementação nova, build, teste de execução ou instalação neste ciclo.
- Entregue: `docs/PS4_FASE4_PENDENCIAS.md`, com estado confrontado com código/logs, 16 partes (P0–P15), dependências, critérios de pronto, ordem recomendada e fronteiras com fases 3/5/6.
- Conclusão: o marco diagnóstico G0 tem evidência desktop; recursos gerais, sincronização do conjunto e apresentação pelo runtime continuam pendentes. G1 operacional é proposta mais ampla que o critério diagnóstico literal.
- Lacunas confirmadas: WRITE_DATA/DMA_DATA existem em decoder/probes separados, mas não no switch GNM. Device headless/readback por draw não equivale a VideoOut integrado. O cache DXIL já existe; faltam integração e persistência adequadas do caminho PS4.
- Verificação: inspeção estática/leitura de logs; os 21/21 e builds citados pertencem aos ciclos anteriores, não foram reexecutados agora.
- Próxima ação exata: P0/P1 mínimo, depois P2 (`s_buffer_load_dwordx4` com descriptor PS4, recurso GPU real e teste de mudança entre submissões). Fase 4 permanece aberta.

## Incremento em execução — transferências no GNM (2026-11-08)

- Estado: CONCLUÍDO para este incremento (validação final abaixo). AGENTS.md e objetivo lidos; checkout com alterações anteriores preservado.
- Escolha pelo código: `SubmitOnWorker` rejeita 0x37/0x50 embora `DecodeTransfers` já valide WRITE_DATA/DMA_DATA. Fechar a ordem transferências/draw no runtime é um incremento menor verificável da parte P9; buffers/texturas P1/P2 continuam separados.
- Contrato: transferências somente dentro de alvos RGBA8 linear já validados por draws do lote; validação completa antes de GPU/commit; ordem serial no mesmo lote. Não implementar CCB, indiretos, DMA memory-copy ou apresentação neste ciclo.
- Critérios: unit tests de ordem/rollback/limites/alias, ELF AMD64 com fill→draw→writes→draw, pixels conferidos, regressão PS5, build UWP e auditoria; nada instalado.
- Evidências e comandos deste ciclo: `build/stage4-transfers-gnm/`. Próxima ação: implementar plano de operações e reutilizar DecodeTransfers.

### Evidência parcial do incremento

- Implementado plano ordenado de operações GNM, resolução de transferências para alvos de draw e executor compartilhado `ApplyTransfers` para snapshots privados. Limites: 4096 transferências, 16 MiB escritos por lote, sem alias com shader/DCB/arrays de comandos em lotes mistos.
- Testes GPU já aprovados em D3D12 Radeon, WARP e Vulkan: fill → draw com alpha → WRITE_DATA → draw, todos os 2048 pixels; zero erros debug/validation. Comando: `tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-shaders/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-transfers-gnm/gpu-tools`; log `gpu.log`.
- As transferências integradas são materializadas em staging CPU privado; uploads/readbacks e draws passam pelos adaptadores GPU existentes. Não é implementação de DMA assíncrono ou novos comandos GPU de cópia geral.
- Build desktop completo em andamento (`cmake --build build/stage1-desktop --target ps4_runtime_tests -j 4`, `build-desktop.log`). Build UWP em `build-uwp.log`. Novo teste ELF e testes negativos escritos, ainda pendentes de execução.
- Próxima ação: finalizar builds, recompilar os objetos modificados após o início do build e relinkar testes com os objetos finais; executar unit/ELF e CTest, corrigindo qualquer falha antes de concluir.

### Execução guest e correção da cobertura negativa

- `unit.log`: casos de ordem/alias/rollback aprovados. `guest.log`: ELF AMD64 completo passou, status 0x600d474e, 2 submissões, 4 draws e 1 rejeição; todos os 2048 pixels conferidos.
- Revisão encontrou teste de orçamento de bytes recusado antes por alias DCB/framebuffer (o DCB grande atravessava o alvo). Corrigido: DCBs grandes dos testes negativos vão para faixa separada, com assertions do motivo `count budget`/`byte budget`. Não foi erro de execução da GPU; era cobertura insuficiente do teste. Próxima ação: reexecutar unit e regressão com essa correção.

### Validação final — CONCLUÍDO o incremento, missão global aberta

- Implementação integrada: WRITE_DATA/DMA_DATA de preenchimento imediato participam da sequência ordenada GNM junto com draws, limitados aos alvos de draw validados no lote. Decoder existente reutilizado, executor de snapshot compartilhado, budgets e alias guards, publicação final somente após sucesso dos draws.
- `unit-final.log`: ordem entre pacotes e DCBs, writes posteriores, endereço fora/desalinhado, controles/comprimento, 4096 pacotes/16 MiB, falta de alvo, aliases/read-only e erro de renderer aprovados. O teste de bytes corrigido agora registra **GNM transfer byte budget exceeded**, além do count budget.
- `guest.log` e `ctest-details.log`: ELF AMD64 → builders/imports GNM → PM4 misto → GCN → GPU → framebuffer guest. Status 0x600d474e; 2 submissões, 4 draws, 1 rejeição; todos os 2048 pixels RGBA conferidos.
- `gpu-final.log`: dez cenas anteriores e uma cena mista em Radeon D3D12, WARP e Vulkan; cenário novo faz dois draws reais com staging entre eles e confere os 2048 pixels. Zero erros debug/validation. Novo readback RGB idêntico nos três renderizadores (`readback-comparison.log`). 18 objetos DXIL validados.
- `build-desktop.log`: núcleo desktop/Vulkan completo aprovado (`cmake --build build/stage1-desktop --target ps4_runtime_tests -j 4`). O executor modificado após o início do build foi recompilado com o comando CMake em cache antes do link; `compile-transfer-final.log`. `build-probes.log`: probes/decoder atualizados. Testes completos relinkados com os mesmos objetos finais e comandos CMake (`relink.log`, `relink-budget-fix.log`, script local `relink-final.py`).
- `ctest-final.log`: **22/22 aprovados**, incluindo ps4_gnm_transfers e regressões PS5 shader_cfg/proveniência/recursos. Comando: `ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'`.
- `build-uwp-final.log`: build completo UWP/D3D12 aprovado via `src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp`. `audit-uwp-final.log`: imports aprovados contra WindowsApp.lib. Nenhum aplicativo instalado/registrado/deployado; AppContainer/Xbox não executados.
- Código/fixtures próprios; referências somente leitura: shadPS4 liverpool.cpp (DmaData/fill), SharpEmu GnmGpuWrites.cs (distinção CPU/GPU). Não foram alterados os projetos de referência.
- Relatório: docs/PS4_GNM_TRANSFERS.md. Limites: staging CPU síncrono, alvos atuais, sem memória arbitrária/SMRD/texturas, DMA copy/async, CCB/CE/DE, indiretos ou VideoOut. PS5 não teve gameplay validado; apenas regressões selecionadas. Fase 4 geral continua aberta.
- Próxima ação exata: P1/P2 mínimo — descriptor de buffer PS4 nos user SGPRs, binding real Vulkan/D3D12 e s_buffer_load_dwordx4; fixture guest altera dados entre submissões e verifica os pixels.

## Incremento buffers/SMRD — CONCLUÍDO no contrato abaixo (2026-11-08)

- Objetivo: descriptor PS4 validado e s_buffer_load_dwordx4 lendo buffer GPU real, com dados alterados entre submissões.
- Regras/objetivo lidos integralmente; fontes e documentos confrontados com o estado atual. Nada instalar/deployar. Preservar alterações anteriores.
- Contrato inicial: um buffer read-only no PS, descriptor direto em s0..s3, stride zero/raw/identity, endereço 40-bit alinhado, até 64 KiB, offset imediato dentro do recurso; s_waitcnt lgkmcnt(0). Sem pointers indiretos, formatos, stores ou texturas neste incremento.
- Reuso: Translator::S_LOAD/ReadConstBuffer, ResourceTracking/CompileProgram/emissor SPIR-V e Mesa-UWP/DxilCompiler. Adaptadores existentes receberão SSBO/SRV real com lifetime até fence.
- Logs/comandos em build/stage4-buffers/. Próxima ação: frontend/validação e teste de compilação SPIR-V; depois bindings e fixture guest.

### Primeira execução dos probes de buffer

- Compilação GCN → IR ReadConstBuffer → SPIR-V validada (`shaders.log`); recurso não é constante CPU.
- `gpu.log`: D3D12/WARP e Vulkan conferiram cores dos dois conteúdos do buffer. Vulkan reportou VUID de push-constant block: IR declara 128 bytes e layout reservava 8. Corrigido layout para 128; reexecução pendente. Não tratar essa primeira execução como aprovação final.
- Integrados descriptor/snapshot ao GNM e dados PS no RasterState. Próxima ação: teste guest e negativos, recompilar ambos os hosts e repetir validação.

### Correção e validação dos backends

- `gpu-final.log`: corrigido layout Vulkan para o bloco completo de 128 bytes; zero erros debug/validation. Os cenários novos 10/11 leem o mesmo SPIR-V/DXIL e dois conteúdos distintos de buffer, com prefixo sentinela e offset DWORD=4; todos os 2048 pixels de cada caso conferidos em Radeon D3D12, WARP e Vulkan. 20 objetos DXIL validados.
- D3D12: buffer default, upload e transição para PIXEL_SHADER_RESOURCE, SRV raw read-only t58/space0, metadata de range nas push constants. Vulkan: SSBO read-only set0/binding58 e range metadata; ambos retêm recursos até fence. O wrapper GCN normaliza apenas seu binding para o ABI comum, sem mudar emissor/IR PS5.
- `build-uwp.log`: build completo UWP/D3D12 aprovado; execução AppContainer/Xbox não realizada. Núcleo desktop completo segue compilando (`build-desktop.log`).
- Testes negativos e ELF de buffer escritos: payload alterado após primeira submissão mantendo descriptor/shader; snapshot de 64 bytes, erro de endereço/stride/formato/bounds/alias sem efeitos. Próxima ação: relinkar com os objetos finais e executar testes/guest/regressão.

### Fechamento do incremento — CONCLUÍDO; missão global aberta

- Implementado descriptor PS4 raw/stride zero/identity (40-bit, alinhamento 16, 16..65536 bytes), s_buffer_load_dwordx4 com offset imediato em DWORDs e s_waitcnt lgkmcnt(0), definição/limites do grupo SGPR e recusa de mutação do descriptor.
- Buffer GPU real integrado ao GNM, IR/ReadConstBuffer e adaptadores: SSBO Vulkan, default buffer/SRV D3D12. Snapshot sob lease por draw, sem constante CPU no lugar do load e sem alterar a IR/emissor PS5. Recusa de alias com alvos do lote.
- `guest.log`/`ctest-details.log`: ELF AMD64 muda payload mantendo descriptor/bytecode; status 0x600d474e, 2 submissões, 2 draws, 1 lote inválido recusado e 2048 pixels finais conferidos. `unit.log`: snapshots mudam, SPIR-V permanece igual, descriptors/endereços/bounds/alias inválidos não executam renderer nem publicam dados.
- `shaders-final.log`: 13 fixtures SPIR-V (mais checks de interface anteriores) e negativos de SMRD/descriptor aprovados; bytecode GCN próprio emitido. `gpu-final.log`: 20 objetos DXIL validados e cenários de buffer com duas cargas de dados em D3D12 Radeon/WARP/Vulkan, todos os pixels; zero erros debug/validation. `readback-comparison.log`: imagens novas idênticas nos três hosts.
- Corrigido durante o ciclo: layout Vulkan reservava 8 bytes para bloco SPIR-V de 128; reportado em `gpu.log`, ajustado para 128 e reexecutado com zero erros em `gpu-final.log`. Não usar a primeira execução como aprovação.
- `build-desktop.log`: núcleo desktop completo + raster Vulkan aprovados (`cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_raster_vulkan_tests -j 4`). `relink.log`: testes completos ligados com os mesmos objetos finais/comandos CMake em cache (`relink-final.py`), evitando duplicar o build de todo o núcleo por teste.
- `ctest-final.log`: **23/23 aprovados**, incluindo ps4_gnm_buffer, transferências, loaders/HLE e regressões PS5 shader_cfg/proveniência/recursos. Comando: `ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'`.
- `build-uwp.log`: build completo UWP/D3D12 aprovado via `src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp`. `audit-uwp.log`: imports contra WindowsApp.lib aprovados. Não executado em AppContainer/Xbox; nada instalado/deployado.
- Código/fixtures próprios; fontes de referência somente leitura: shadPS4 decode.cpp/scalar_memory.cpp/resource.h e SharpEmu GcnBufferDescriptor.cs. Relatório em docs/PS4_GCN_BUFFERS.md.
- Limites: um buffer read-only no PS, descriptor direto s0..s3 e offset imediato; sem múltiplos buffers, VS/compute, tabelas indiretas, stride/formatos, stores, texturas, coerência de alias ou apresentação. PS5 gameplay não testado; regressões selecionadas passaram. Fase 4 geral continua aberta.
- Próxima ação exata: ampliar binding/seleção de descriptors e loads vetoriais/vertex fetch MUBUF com dados de geometria guest, mantendo offsets/limites e validação GPU nos dois backends.

## 2026-11-08 — Interpolação e cobertura GCN (EM EXECUÇÃO)

Investigado decoder próprio, wrapper GCN, estado GNM e interpolação na IR compartilhada. Incremento escolhido: mapa smooth de entradas PS para PARAM exportado, preservando limites e transações; expansão VOP1 com operações já traduzidas pela IR. As demais famílias serão inventariadas com dependências; não há alegação de suporte geral. Próxima ação: fixtures de mapeamento não identity e ALU, build e readbacks D3D12/WARP/Vulkan, regressão PS5 e build UWP. Nenhuma instalação autorizada ou realizada.

Validação intermediária: mapa PARAM e sete VOP1 passaram em 14 cenas por backend. A extensão linear central (SPI_PS_INPUT_ENA/ADDR=32) compilou, mas o primeiro readback apontou erro na referência CPU: ela ainda multiplicava green por 1/W do modo perspectiva. Os atributos VS são exportados antes da multiplicação da posição por W; no modo linear a referência correta é green=v. Corrigida somente a referência e programada repetição nos três renderizadores. Log inicial: build/stage4-interpolation/gpu-final.log (será preservado como gpu-linear-reference-error.log). Build desktop incremental e build UWP anteriores aprovados; checagens finais em andamento.

## 2026-11-08 — Interpolação e ALU GCN (incremento CONCLUÍDO; objetivo geral INCOMPLETO)

Implementados no frontend GCN: mapa opcional smooth de até quatro entradas PS para PARAMs exportados e qualificador linear central. GNM lê e valida o mapa, permite ENA/ADDR=2 (perspectiva) ou 32 (linear), rejeita inconsistências e flags não suportadas antes do renderer. Decoder acrescenta CVT_F32_I32, FRACT_F32, TRUNC_F32, CEIL_F32, FLOOR_F32, RCP_F32 e SQRT_F32, reutilizando traduções IR existentes. Nenhuma mudança de semântica PS5 ou backend compartilhado.

Arquivos de código: GcnCompiler.{h,cpp}, frontend/decode/GcnDecoder.cpp, libs/ps4Gnm.cpp. Testes: Ps4GcnShaderTests.cpp, Ps4GcnDxilTests.cpp, Ps4RasterTests.cpp, Ps4GnmTests.cpp, run-ps4-gcn-gpu.ps1. Relatório e matriz de todas as famílias: docs/PS4_GCN_COVERAGE.md; cabeçalhos atuais dos documentos obrigatórios e plano da fase 4 atualizados.

Comandos/evidências em build/stage4-interpolation/: C:/msys64/ucrt64/bin/python.exe build/stage4-interpolation/build-tests.py ps4_gcn_shader_tests ps4_gnm_tests shader_cfg_tests ps4_runtime_tests recompilou os três fontes de produção alterados e quatro entradas de testes, reutilizando demais objetos já compilados (build-final.log); relink.py ps4_gcn_shader_tests atualizou a fixture final com carga real de buffer (relink-shader-final.log). CMake --build build/stage1-desktop --target ps4_raster_vulkan_tests -j4 passou (build-vulkan-final.log). CTest --test-dir build/stage1-desktop --output-on-failure com regex dos 23 testes documentados passou 23/23 em 5,75s (ctest-final.log; detalhes copiados). Não foi executado rebuild limpo de todo o aplicativo desktop.

ps4_gcn_shader_tests.exe build/stage4-interpolation/fixtures gerou 16 módulos SPIR-V validados (shaders-final.log). tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-interpolation/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-interpolation/gpu-tools validou 26 objetos DXIL SM6.4, 15 variantes e cenário misto em D3D12 RX6600M/WARP/Vulkan (gpu-buffer-alu-final.log), todos 2048 pixels RGBA conferidos, zero erros nas camadas gráficas. unary.spvasm prova loads de buffer e sete operações efetivas; linear.spvasm NoPerspective e remap.spvasm Location3. Novos readbacks: hardware D3D12/Vulkan idênticos; WARP até 1 nível RGB nos gradientes, ALU idêntica (readback-comparison.log).

src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp passou incremental, incluindo selfTest D3D12 com nova assinatura e layout atualizado (build-uwp-final.log). build/stage3/audit-uwp-imports.ps1 aprovou WindowsApp.lib (audit-uwp.log). Nenhuma instalação/deploy nem execução AppContainer/Xbox. Regressões PS5 shader_cfg, scalar_provenance, tracking/materialization e demais testes existentes aprovados; gameplay PS5 não exercitado.

Erros corrigidos: referência CPU linear mantinha fator 1/W no verde; corrigida para interpolação afim dos atributos exportados, repetidos três backends (falha preservada gpu-linear-reference-error.log). Edição auxiliar da fixture parou com ValueError por espaçamento alterado por clang-format; inspecionada e reaplicada antes de compilar/testar. Atualização documental interrompida por Python usando cp1252 como padrão; repetida com UTF-8 explícito, normalizadas apenas as duas entradas novas de PROGRESS que haviam sido anexadas em cp1252. Documentos e código verificados ao fim. Nenhuma alteração anterior do usuário foi revertida.

Referências somente leitura: shadPS4 opcodes.h/decode.cpp/regs_shader.h (VOP1, famílias e bits central/linear); SharpEmu GcnShaderDecoder.cs (perfil GFX6/7 separado da IR compartilhada). Implementação/fixtures próprias; nenhum código externo copiado.

Limites e retomada: incremento CONCLUÍDO no contrato descrito; interpolação completa, todas as famílias GCN e missão PS4 permanecem INCOMPLETAS. Pares P1/P2 ainda canônicos, sem MOV/flat/sample/centroid; contagem VS/PS ainda acoplada. VOPC/EXEC, MUBUF/MTBUF, DS e MIMG ausentes. Novos modos passam pelo estado GNM no teste unitário e por GPU nos probes; não foi executado ELF guest usando mapa/linear novos. Próxima tarefa exata: MUBUF read-only indexado no binding raw real, decoder de endereço/index com bounds, estado GNM e payload/índices variáveis entre submissões, readback completo nos três renderizadores. Implementar VCC/EXEC + VOPC antes de divergência geral. Sem bloqueio externo no incremento entregue; não habilitar lançamento normal/UI nem alegar compatibilidade comercial.

Conferência final automatizada: evidence-summary.log confirma 26 DXIL, 45 readbacks, três cenários mistos e zero erros gráficos. A primeira captura de audit-uwp.log com > ficou vazia porque a mensagem usa o fluxo de informação do PowerShell; auditoria reexecutada com *> e PASS capturado no arquivo.

## 2026-11-08 — MUBUF indexado / buffers e vertex fetch (EM EXECUÇÃO)

Investigado decoder GCN, materialização/IR de buffers, bindings arrays e probes existentes. Incremento: loads MUBUF DWORD/DWORDX4 indexados com stride validado e dois buffers read-only no estágio que usa memória, incluindo VS para geometria guest. Preservar limite de um estágio com recursos por draw neste primeiro contrato; texturas/imagens/samplers ficam pendentes. Próxima ação: decoder, guardas de índice antes de multiplicação, ABI de até dois descriptors, snapshots GNM e readbacks GPU. Sem instalação.

### MUBUF — implementação e primeira validação

- GcnDecoder aceita BUFFER_LOAD_DWORD (12) / DWORDX4 (14), IDXEN, offset alinhado em bytes, SOFFSET inline zero, sem cache/TFE/LDS/OFFEN/ADDR64; duas origens diretas s0..3/s4..7. Definição e RSRC1 verificam o grupo inteiro de destino/descriptor. WAITCNT inclui vmcnt(0).
- GcnCompiler mantém até dois descriptors na ordem dos recursos da IR; stride estruturado e tamanho num_records*stride são validados em 64 bits. Insere UMin32 no índice antes de multiplicar por stride: o índice inválido vira record sentinela fora da faixa, evitando wrap para dados válidos. Não muda o emissor/IR PS5.
- GNM captura dois payloads sob lease, valida ranges/alias de ambos e recusa buffers simultâneos em VS e PS neste contrato. Probes D3D12/Vulkan ligam arrays de dois buffers read-only com lifetime até fence; estágio VS agora pode ler dados.
- build/stage4-mubuf/shaders.log preserva falha inicial de ABI: metadata de dois recursos ocupa três DWORDs (um DWORD de offsets de 8 bits compactados, seguido de dois tamanhos), não quatro. Corrigido no wrapper e nos hosts; shaders-final.log passou, incluindo três fixtures novas e negativos.
- gpu.log: 32 objetos DXIL SM6.4 validados, 23 cenas por D3D12 Radeon/WARP/Vulkan e cenário misto de transferências em cada host; zero erros debug/validation. Índices 4, 0x10000000 e UINT32_MAX retornaram zero; índice válido selecionou cores distintas. Vertex fetch buscou posições/cores em dois buffers; atualização de ambos mudou cobertura 576→256 pixels e gradiente. Todos os 2048 pixels RGBA conferidos por cena.
- build-uwp.log: UWP/D3D12 compilado e layout gerado; nada instalado/deployado. unit.log: dois snapshots independentes, updates, shader estável, descriptor secundário inválido/alias/group recusados sem efeitos para PS e VS. Compilação do teste inicialmente recusou auto combinado de tipos uint32/uint64 (relink.log); separado e corrigido em relink-final.log.
- Build desktop completo em andamento: cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_raster_vulkan_tests -j4 (build-desktop.log). Necessário recompilar também objetos do host que dependem do novo layout RasterState. Não executar ELF/readbacks por core com layout antigo. Próxima ação após build: relink dos testes com objetos finais, --indexed ELF guest, 24 testes CTest e auditoria UWP.

## 2026-11-08 — MUBUF / dois buffers / vertex fetch — incremento CONCLUÍDO; objetivo geral INCOMPLETO

Contrato funcional integrado em GcnDecoder.*, GcnCompiler.*, ps4Gnm.cpp, ps4Raster.h e probes D3D12/Vulkan. Até dois buffers read-only diretos em VS ou PS; MUBUF DWORD/DWORDX4 IDXEN, stride/offset/range validados, guarda do índice antes da multiplicação e zero em OOB/overflow; vertex fetch inline de posições/cores. Recursos simultâneos VS+PS recusados explicitamente. Texturas/MIMG e o restante da fase 4 não implementados por este incremento.

Testes/fixtures atualizados: Ps4GcnShaderTests.cpp, Ps4GcnDxilTests.cpp, Ps4RasterTests.cpp, Ps4GnmTestFixture.h, Ps4GnmTests.cpp, tests/run-ps4-gcn-gpu.ps1 e novo CTest ps4_gnm_indexed em CMakeLists.txt. Documentação: docs/PS4_MUBUF.md, cabeçalhos atuais dos documentos obrigatórios, cobertura de famílias e partes P1/P3.

Evidências finais em build/stage4-mubuf/: shaders-final.log confirma 19 SPIR-V e negativos; gpu-final.log confirma 32 DXIL SM6.4 e 69 readbacks (23 variantes em Radeon D3D12, WARP e Vulkan), mais três cenários mistos de transferências/draws, zero erros gráficos. GPU verifica índices 1/2, limite 4, 0x10000000 e UINT32_MAX, geometria fetch modificada (576→256 pixels) e quatro loads DWORD com offsets 0/4/8/12. Todos os 2048 pixels RGBA conferidos. readback-comparison.log: hardware D3D12/Vulkan idênticos nos oito casos novos; WARP até um nível RGB no gradiente de fetch, demais novos idênticos. indexed.spvasm/fetch.spvasm mostram loads reais, array de dois buffers e UMin antes de IMul. evidence-summary.log confere os totais.

Guest: build/stage1-desktop/ps4_gnm_tests.exe build/stage4-mubuf/guest --indexed passou (guest.log): ELF AMD64 → imports GNM → SMRD índice em buffer zero → MUBUF em buffer um → GPU → framebuffer guest. Guest muda índice e payload entre submissões mantendo descriptor/bytecode; status 600d474e, duas submissões, dois draws, uma rejeição esperada e 2048 pixels finais aprovados. unit-final.log comprova snapshots independentes, módulo estável em ambos os estágios, recusa sem efeitos de segundo descriptor inválido/inacessível/alias, alocação insuficiente e buffers simultâneos VS/PS. Fetch VS chega ao GNM no teste unitário e executa em GPU nos probes; não declarar ELF novo com fetch VS.

Compilação: cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_raster_vulkan_tests -j4 completou o core desktop (build-desktop.log). Ao repetir, Ninja reportou metadata “premature end of file; recovering” e reabriu recompilação ampla; a limpeza de macro por formatação havia omitido GetImageMemoryRequirements e a compilação do probe falhou (build-desktop-final.log). Corrigida a entrada, preservadas as 50 funções Vulkan em macro com formatação estável. Compilados os objetos finais do probe no core e standalone, mais teste raster, com comandos do compdb (build-final-hosts.log); quatro executáveis ligados com objetos finais pelo link-only.py (link-final.log). Não tratar a repetição falha como aprovação. Não foi executado -t compdb nem outro Ninja no mesmo diretório enquanto aquele build estava ativo; a metadata gerada requer atenção se continuar reconstruindo tudo. Interface desktop completa não foi compilada neste ciclo.

Com o core final: ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$' aprovou 24/24 em 5,70s (ctest-final.log, ctest-details-final.log). Inclui novo ps4_gnm_indexed, modos GNM anteriores, loader/HLE/plataformas e PS5 shader_cfg/scalar_provenance/resource_tracking/materialization. Gameplay PS5 não exercitado. Repetição final tests/run-ps4-gcn-gpu.ps1 com FixtureRoot build/stage4-mubuf/fixtures, TranslatorRoot build/stage3/spirv_to_dxil-dist, VulkanRoot C:/VulkanSDK/1.4.357.0 e OutputDir build/stage4-mubuf/gpu-tools aprovada depois da correção da macro e dos negativos adicionais de host.

UWP: src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp passou (build-uwp-final.log); build/stage3/audit-uwp-imports.ps1 capturado com *> aprovou WindowsApp.lib (audit-uwp.log). Nada instalado/deployado, sem execução AppContainer/Xbox. Erros intermediários preservados: ABI dois buffers = três DWORDs, auto misto uint32/uint64 no teste e macro Vulkan; todos corrigidos e revalidados. clang-format --dry-run --Werror nos fontes alterados aprovado após isolar a macro da formatação instável.

Referências somente leitura: shadPS4 frontend/decode.cpp/opcodes.h e video_core/amdgpu/resource.h; SharpEmu GcnBufferDescriptor.cs e GcnShaderDecoder.cs. Conferidos campos MUBUF/stride/num_records e separação GCN/IR; implementação e fixtures próprias, sem cópia.

Retomada exata: implementar descriptor de textura PS4 2D linear RGBA8 read-only e snapshot com pitch/bounds, upload/recurso de imagem real Vulkan/D3D12, IMAGE_LOAD e padrão legal com atualização entre submissões e todos os texels conferidos; depois sampler nearest/clamp e IMAGE_SAMPLE_L. Para ampliar múltiplos buffers além do contrato atual, separar a metadata VS/PS e testar uso simultâneo antes de remover a rejeição explícita. Incremento entregue CONCLUÍDO; texturas, recursos gerais, apresentação e compatibilidade PS4 global INCOMPLETOS.

## 2026-11-08 — IMAGE_LOAD 2D RGBA8 e recursos VS+PS (EM EXECUÇÃO)

Lidos AGENTS/especificação e estado dos contratos. Investigados IMAGE_LOAD→ImageRead→OpImageFetch na IR, descriptor GCN de imagem de oito DWORDs e ABI push-data. Incremento: até uma imagem 2D linear RGBA8 por estágio, dois buffers por estágio com bindings/metadados independentes. Validar pitch/formatos/bounds/alias; dados reais de imagem nos dois hosts, fixtures próprias e GNM. Nada instalado. Próxima ação: decoder/descriptor, bindings separados e uploads.

### IMAGE_LOAD — implementação e evidências intermediárias

Implementados descriptor GCN de oito DWORDs (2D RGBA8 UNORM, linear-aligned, um mip/layer), IMAGE_LOAD de quatro canais em VS/PS, verificação de grupos VGPR/SGPR e imutabilidade do descriptor. IR existente ImageRead gera OpImageFetch; wrapper GCN insere guarda X/Y antes do fetch, retornando zero em OOB. Bindings PS buffers58/textura64 e VS buffers60/textura65, metadata PS DWORD0 e VS DWORD4. Sem alterar a IR/emissão ou ABI PS5. GNM captura quatro buffers e duas texturas com lease, recusa alias e limita snapshots acumulados a 16 MiB antes de executar GPU.

Hosts implementam recursos reais: texturas default/upload/SRV D3D12 e imagens sampled/transfer/layout/view Vulkan, com row pitch guest explícito, buffers e metadata disjuntos e retenção até fence. gpu.log: 34 objetos DXIL, 59 variantes por host (177 readbacks), três cenários mistos, zero erros em D3D12 Radeon/WARP/Vulkan. Casos novos conferem todos os 32 texels PS, padding, mudanças simultâneas de dados/posição e OOB X/Y/UINT32_MAX; todos os 2048 pixels RGBA por variante. Comparação em readback-comparison.log: máximo um nível RGB.

shaders-final.log: 21 SPIR-V validados e negativos de descriptor/controles/grupos/imutabilidade com outputs transacionais. unit-final.log: seis snapshots atualizados independentemente, módulos estáveis, erros de descriptor/endereço/alias e orçamento rejeitados antes do renderer. guest-final.log: ELF AMD64 com os seis recursos simultâneos, duas submissões/dois draws/uma rejeição, status600d474e, todos os pixels aprovados. Primeiro guest completou as chamadas, mas a referência CPU não preservava os pixels do primeiro triângulo fora do segundo menor; corrigida a referência e repetido (guest-reference-error.log preservado). Uma edição auxiliar parou em assertion de espaçamento; reaplicada antes do build. Primeiro build apontou uso incorreto do acessor IR::Value::Inst; corrigido para ResolveInstruction, sem alteração da IR compartilhada.

build-native.log: três fontes do frontend/GNM e dois hosts Vulkan recompilados via comandos existentes em compdb; quatro testes ligados aos objetos do core. build-vulkan.log: probe standalone recompilado/ligado com o contrato atual. configure-desktop.log: CMake reconfigurado para registrar novo ps4_gnm_textures. ctest.log: 25/25 aprovados (7,07s), incluindo novo guest, regressões PS4 e shader/resource/plataforma PS5; detalhes copiados. build-uwp.log e audit-uwp.log aprovados, sem instalação/deploy. Próxima ação: checagem final do build UWP/current tree e documentação/limites. Não declarar gameplay ou Xbox.

## 2026-11-08 — IMAGE_LOAD 2D RGBA8 + texturas/buffers simultâneos VS/PS — incremento CONCLUÍDO

Objetivos deste incremento implementados e validados: até dois buffers e uma imagem RGBA8 linear por estágio no mesmo draw, com bindings/metadata separados; IMAGE_LOAD gera fetch real de textura, guardado em OOB, nos dois hosts e pelo GNM/runtime. Limites e fase 4 geral continuam parciais. Relatório completo: docs/PS4_TEXTURES.md; documentos obrigatórios/objetivo, cobertura MIMG e partes P1/P6/P7 atualizados.

Arquivos: GcnCompiler.{h,cpp}, GcnDecoder.{h,cpp}, ps4Gnm.cpp, ps4Raster.h, probes D3D12/Vulkan, testes GCN/DXIL/raster/GNM/ELF, novo Ps4TextureFixture.h, script GPU e CMakeLists (ps4_gnm_textures). Descriptor T# direto de oito DWORDs, formato/tipo/tiling/pitch/limites estritos, imutabilidade e grupos completos. Recursos readonly capturados sob lease, alias de alvo recusado, orçamento cumulativo 16MiB verificado durante preparo; retenção até fence. Não foram alteradas passes/emissão/ABI PS5 compartilhadas. Comentários de API ajustados ao fim sem mudança funcional.

Resultado real: evidence-summary.log em build/stage4-textures confirma 21 SPIR-V, 34 DXIL SM6.4, 177 readbacks e três cenas mistas, 25/25 CTest e guest600d474e, zero erros gráficos, auditoria UWP PASS. GPU: todos os 32 texels PS de imagem8x4/pitch256, duas texturas e quatro buffers distintos, alteração de dados/posição, OOB X/Y/UINT32_MAX; 2048 pixels RGBA por variante. readback-comparison.log compara 72 pares novos com delta máximo1RGB. texture-vs.spvasm e texture-ps.spvasm mostram OpImageFetch e bindings60/65 versus58/64, com guards.

Comandos/compilação: C:/msys64/ucrt64/bin/python.exe build/stage4-textures/build-tests.py ps4_gnm_tests ps4_gcn_shader_tests shader_cfg_tests ps4_runtime_tests recompilou os cinco fontes alterados/dependentes e ligou testes aos demais objetos do core existentes (build-native.log). relink.py atualizou fixtures/negativos (relink-tests.log, relink-gnm-final.log); build-vulkan.log confirma compilação/link do probe standalone por comandos CMake em cache. CMake -S . -B build/stage1-desktop registrou novo teste (configure-desktop.log). Não houve rebuild limpo da interface/core desktop inteiro; execução desktop Windows/Vulkan, sem Linux neste ciclo.

build/stage1-desktop/ps4_gnm_tests.exe build/stage4-textures/guest --textures aprovado em guest-final.log: ELF AMD64 inicializa e altera os seis recursos, executa dois draws/submissões, recusa lote inválido, status600d474e; imagem final inteira aprovada, incluindo pixels preservados do primeiro draw fora do segundo. unit-final.log confirma seis snapshots independentes e módulos estáveis; negativos de descriptor/endereço/alias e orçamento recusados sem renderer/escrita. shaders-final.log valida módulos/negativos transacionais. ctest --test-dir build/stage1-desktop --output-on-failure com regex PS4+regressões selecionadas aprova25/25 em7,07s (ctest.log, ctest-details.log): PS4 anterior, novo ps4_gnm_textures, PS5 shader_cfg/scalar_provenance/resource tracking/materialization e demais testes de plataforma/loader/HLE. Gameplay PS5 não executado.

Script tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-textures/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-textures/gpu-tools aprovado em gpu.log: Radeon D3D12, WARP e Vulkan, zero debug/validation. src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp aprovou build completo/layout em build-uwp-final.log; build/stage3/audit-uwp-imports.ps1 capturado com *> aprovou WindowsApp.lib (audit-uwp-final.log). Nenhuma instalação/deploy ou execução AppContainer/Xbox.

Erros corrigidos e preservados: acessor IR::Value::Inst inexistente substituído pelo ResolveInstruction existente (primeiro build); edição interrompida por espaçamento retomada antes de compilar. Primeiro ELF chegou ao status esperado, mas referência CPU ignorava preservação de pixels fora do segundo triângulo menor; corrigida referência, repetidos guest/CTest, mantido guest-reference-error.log. Nenhuma alteração externa nem reversão de trabalho anterior.

Referências somente leitura: shadPS4 decodeInstructionMIMG/opcodes/instruction e resource.h/pixel_format.h/tiling.h; SharpEmu GcnImageDescriptor.cs (T# GCN não equivale a RDNA2). Código/fixtures próprios sem copiar código das referências. Próxima tarefa exata: sampler PS4 nearest/clamp + IMAGE_SAMPLE_L com LOD0 explícito e bordas/orientação/updates conferidos nos dois hosts pelo GNM; depois mips/formatos/tiling/coerência. Sem texturas gerais, apresentação, compatibilidade comercial ou Xbox comprovados. A missão PS4 global permanece INCOMPLETA.
