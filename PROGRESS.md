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

## 2026-11-08 — Auditoria Lumen para a fase 4 (EM EXECUÇÃO)

Pedido atual é análise e atualização documental, sem implementação. Lumen mantido somente leitura. Identificadas árvores Gen4/shadPS4, Gen5 engine/KytyPS5 e legado/compatibility; seleção de build e notices conferidos. Inventário estático/hash em build/lumen-audit/snapshot.json, sem Git na raiz da referência. Investigados samplers/IMAGE_SAMPLE, fetch, ISA/EXEC, layouts/tiling, publicação CPU/GPU, barreiras e leases, PM4, scheduler, VideoOut, testes e adaptações Metal/mobile. Próxima ação: concluir matriz P0..P15, prioridades/limites de portabilidade e validar referências dos documentos. Nenhum build/teste/instalação Lumen ou mudança no código Kyty.

## 2026-11-08 — Auditoria Lumen da fase 4 — CONCLUÍDA (documentação)

Atendido o pedido de análise do checkout C:/Users/rodri/Dev1/Projetos/lumen-emu-main e atualização documental. Criado docs/PS4_LUMEN_GPU_AUDIT.md, com identidades Gen4/Gen5/legado, mapa verificável de arquivos/símbolos, contribuições P0..P15, dependências/prioridades, testes existentes, cuidados reais de host/licença e próximo incremento. Atualizados índices/cabeçalhos de CODEX_PS4_COMPATIBILITY_GOAL.md, docs/PS4_COMPATIBILITY.md, PS4_GPU.md, PS4_FASE4_PENDENCIAS.md, PS4_GCN_COVERAGE.md, PS4_TEXTURES.md e PS4_GNM.md. Nenhum código/teste/build-config Kyty alterado; nenhuma cópia de código Lumen.

Achados: sampler/IMAGE_SAMPLE e formatos/views GCN são úteis imediatamente, mas IR e caches Vulkan/D3D12 já existem no Kyty; faltam normalização/bindings PS4. Publication snapshots/journals, barreiras produtor→detiler e leases/ticks oferecem contratos para coerência/staging futuros. VideoOut/PM4 e ledger ajudam a trilha de primeira imagem. Fetch/ISA/EXEC/interpolação/depth/compute e fallbacks Metal contribuem como referências separadas por estágio. Não importar skips de waits/indiretos inválidos, sondagem de mapping de um byte como prova de payload, seletores experimentais ou adaptações Android/Metal como suporte D3D12/UWP pronto.

Evidência estática: build/lumen-audit/snapshot.json inventaria742 arquivos com SHA256 e tamanho, verification.json confirma referência inalterada e fingerprint12ea8c08998bac9d1d4552109f0d42c82bc6b3e82aebb9008aad9543831a148a. Raiz Lumen sem .git; revisões de origem lidas em UPSTREAM.json, não tratadas como HEAD real de todo snapshot. Engine graphics contra Kyty:216 caminhos comuns,48 iguais após CRLF/LF,168 textos distintos,15 só naquela árvore; isso não determina novidade/autoria/melhoria semântica. Arquivos e símbolos relevantes lidos com Get-Content/rg; manifests parseados e hashes relidos. Buscas inválidas de brace expansion/caminhos presumidos corrigidas antes de usar evidências.

Licenças conferidas por avisos locais: integração GPLv2-only, Gen4 GPLv2-or-later com seleção v2, herança Kyty MIT e exceções por arquivo (publication snapshots MIT). Registradas ressalvas do LICENSES.md sobre terceiros sem concluir aprovação de redistribuição. Referência somente leitura; nenhum build/teste/jogo/APK/driver Lumen executado ou instalado. Baseline anterior25/25 Kyty apenas conferido no código/documentos/evidence-summary existente, sem reexecução por ser mudança documental. Próxima implementação recomendada: sampler PS4 nearest/clamp + IMAGE_SAMPLE_L com LOD0, preservando recursos VS/PS e testando bordas/updates nos três hosts e ELF GNM; trilha seguinte de VideoOut linear. Fase4 global permanece INCOMPLETA.

Validação final documental: script Python (C:/msys64/ucrt64/bin/python.exe) aprovou UTF-8 dos nove documentos, 13 links do relatório sem destino ausente e referências de índice resolvidas. Conferência corrigiu nomes presumidos para resource_discover_pass.cpp/resource_patching_pass.cpp, render_end_attachment_barrier.h e videoout/driver.cpp. Rehash dos742 arquivos confirmou zero mudanças Lumen. Evidência: build/lumen-audit/document-validation.json. git diff --check limitado aos nove documentos terminou com exit0. Nenhum teste de compilação/runtime foi executado por se tratar de análise documental.

## 2026-11-08 — Integração das contribuições Lumen — EM EXECUÇÃO

Pedido: integrar as contribuições P0..P15 e testar somente após integrar todas. Ordem respeitada: sem build, validador de shader, CTest ou execução GPU nesta fase. Referência Lumen somente leitura; implementar adaptações no núcleo Kyty, sem importar outro runtime ou substituir backends. Primeiro caminho encontrado: SMRD restrito a DWORDX4/PS/base0 apesar de a IR suportar cinco larguras e ambos estágios. Integração inicial: ampliar decoder, análise de definição, descriptors e limites RSRC1, preparar fixtures e casos negativos. Nenhuma capacidade nova considerada validada. Demais frentes continuam pendentes; estado global INCOMPLETO.

Integração em andamento (sem execução de testes): GcnDecoder agora normaliza as cinco larguras SMRD em VS/PS, base direta s0 ou s4, usando data_dwords no fluxo de definição. GcnCompiler valida fim do acesso pela largura real, preserva descriptors de grupos inteiros e encaminha para a IR já existente; RegisterLimits verifica descriptor/destino completos. Ampliadas dez VOP1 e nove VOP2 com correspondência explícita GCN para operações já existentes; sem EXEC/VCC geral e sem alegar precisão de casos especiais.

P7 inicial: encoding IMAGE_SAMPLE_L normalizado a IMAGE_SAMPLE/ImageSampleFlagLod, coordenadas u/v/lod definidas, S# s4..7 validado exatamente como nearest/clamp normalizado/min-maxLOD0; T# s8..15 continua RGBA8 linear/mip único. Bindings sampler PS66/VS67; imagens64/65 e buffers58/60 preservados. Samplers estáticos D3D12 e imutáveis Vulkan nos adaptadores existentes. Neste perfil sampled cabe um buffer por estágio; o perfil IMAGE_LOAD com dois buffers por estágio permanece separado. Não houve reinterpretação GCN como descriptor Prospero nem cópia de implementação GPL.

P0 diagnóstico: GnmSession::DrawEvidence fornece até256 linhas de metadados (tentativa/ordinal/DCB/PC/módulos/bytes/target) e flags independentes host_completed, guest_published, batch_rejected. Flags anotadas nos pontos reais de retorno do renderer e publicação; falha de builder não reclassifica submissão anterior. Não retém payloads, não prova execução GPU por invocation e não é sincronização/coerência.

Preparados, NÃO EXECUTADOS: testes SPIR-V por largura/estágio/descriptor e ALU; sampler VS/PS, negativos e preservação de outputs; DXIL linked variante17; raster sampled variantes59..61 com referência CPU (dois hosts, três dispositivos no runner existente), pitch, updates e UV fora de[0,1]; GNM recording renderer para sampled e histórico/limite/falhas. Os textos PASS nos fontes são expectativas futuras, não resultados desta execução. Nenhum compilador, validador SPIR-V/DXIL, CTest, probe GPU ou UWP invocado nesta etapa. Comandos usados até aqui apenas Get-Content/rg/git status e scripts Python de edição.

Critério de fechamento esclarecido pelo usuário: TODO o suporte GPU disponível no Gen4 local do Lumen. Não se limita a uma fixture por P0..P15. Manifesto estático build/lumen-integration/gen4-scope.json registra 272 arquivos de shader_recompiler/video_core/gnmdriver/videoout por tamanho/SHA256; inventário não é execução de teste. Essa definição substitui a hipótese inicial de fechar contratos limitados. Testes continuam adiados até a integração completa. Próxima ação: descriptors indiretos/S_LOAD e tabela de recursos, depois integração de todos os demais estágios/pipelines/PM4/layouts/coerência/compute/apresentação Gen4; nenhum desses tópicos pode ser marcado pronto só por existir nas referências.

## Checkpoint desta execução — INCOMPLETO / NÃO VALIDADO

A integração integral do suporte GPU Gen4 não foi concluída nesta sessão. Código escrito cobre partes de P0/P2/P4/P7 e preparação de P14; P1/P3/P5/P6/P8..P13/P15 gerais permanecem pendentes. Não há bloqueio externo diagnosticado; há trabalho de implementação substancial restante. Não afirmar suporte Gen4 completo, incremento validado, regressão PS5 aprovada, UWP compilável ou Xbox funcional para este estado. Preservado o perfil normal PS4 bloqueado e a separação lógica GCN/RDNA; isso não substitui testes de regressão.

Arquivos de código editados nesta execução: src/graphics/shader/recompiler/frontend/decode/GcnDecoder.h/.cpp, GcnCompiler.h/.cpp, src/libs/ps4Gnm.h/.cpp, src/graphics/host_gpu/d3d12/ps4RasterProbe.cpp, src/graphics/host_gpu/renderer/ps4TransferProbe.cpp. Testes preparados em tests/Ps4GcnShaderTests.cpp, Ps4GcnDxilTests.cpp, Ps4GnmTests.cpp, Ps4RasterTests.cpp e run-ps4-gcn-gpu.ps1. Documentação de retomada em docs/PS4_LUMEN_INTEGRATION.md, com links nos oito documentos de especificação/auditoria relevantes. Fontes anteriores não revertidos; sem commit, instalações ou alterações externas.

Retomada precisa: ler docs/PS4_LUMEN_INTEGRATION.md e build/lumen-integration/gen4-scope.json (272 arquivos Gen4, escopo não inclui todos os terceiros/testes). Continuar pelos descriptors indiretos/S_LOAD com leitura bounded e identidade de backing, preservando IR/materialização atuais; integrar os estágios/fetch/lanes/formatos/layouts/PM4/coerência/compute/VideoOut faltantes segundo o critério TODO o suporte GPU Gen4. O sampled inicial usa s0..3 para um buffer, s4..7 sampler e s8..15 imagem por estágio; dois buffers com sampler exigem indireção, não inventar20 user SGPRs no perfil PS4. Preparar também ELF AMD64 específico de sampling, readbacks numéricos das novas ALU e sincronização real. Só após completar a integração requerida executar a bateria desktop/SPIR-V/DXIL/readback/ELF/regressões/UWP; a ordem de testes continua a instrução explícita do usuário. Os binários e logs históricos não foram regenerados e NÃO representam essas alterações.

## 2026-11-08 — Continuação P0–P15 — EM EXECUÇÃO, sem testes antecipados

Releitura de AGENTS/especificação/checkpoint e fontes reais. Implementadas adaptações próprias, sem copiar o renderer Lumen: preâmbulo S_LOAD_DWORD/X2/X4/X8/X16 especializado a partir de leitura guest fornecida pelo runtime sob lease; máximo64 leituras/4KiB por estágio, intervalo inteiro, endereço40bits/alinhamento e grupo104SGPRs internos; ABI permanece16 user SGPRs. Descriptors MUBUF/MIMG/sampler podem ocupar slots internos carregados pela tabela. FrozenScalars preserva desconhecidos e impede mutação posterior de descriptors; branches no corpo mantêm deslocamentos relativos. S_LOAD fora do preâmbulo, offsets dinâmicos e múltiplas imagens diferentes continuam recusados. Output opcional de código normalizado usado por RegisterLimits, não recompila/rele o preâmbulo para conferir alocação.

Gnm compila com callback Read existente, registra ranges de tabelas e recusa alias de tabelas, shaders ou streams de comandos com qualquer target antes de renderizar. Payloads de tabelas entram no orçamento agregado de recursos. Fixture TableSampledTextureFixture usa duas user SGPRs com ponteiro para80bytes, dois buffers e T#/S# em SGPRs internos por estágio; preparados SPIR-V/DXIL e raster62..64 com quatro buffers/duas imagens/samplers nos dois estágios e referência CPU. Nenhum desses testes executado.

PM4: parser de INDIRECT_BUFFER gráfico incondicional0x3f com VMID0, chamadas/chain, leitura completa e snapshot, orçamento4MiB compartilhado com DCBs, até256 IBs e profundidade8; ciclos/endereço/alinhamento/controle inválidos recusados, sem skips de falso sucesso. Evidência de draw passa a incluir endereço do stream aninhado. Casos de chamada, tail chain, ciclo, unmapped/truncamento/limites e lote com tail inválido preparados no recording renderer, não executados. CCB/IB condicional/compute e waits/releases gerais ainda pendentes.

Interpolação: mapa SPI validado por rotina comum GCN/GNM (offset, use_default, default_value e flat); V_INTERP_MOV P0 com flat é normalizado ao atributo nativo flat com primeiro provoking vertex no contrato atual, sem extensão de barycentrics. Quatro defaults RGBA constantes normalizados antes da IR. P1/P2 para flat, MOV suave, flags desconhecidas continuam recusados; intermediários P1, modos MOV P10/P20, centroid/sample e last-provoking ainda pendentes. Fixtures SPIR-V e linked DXIL19..23, oracle raster65..69 preparados, sem execução.

Comandos desta continuação: Get-Content/rg para leitura, scripts Python UTF-8 para edição. Sem compilador, CTest, SPIR-V/DXIL validator, probes, UWP, instalações ou deploy. Não reutilizar os logs25/25 anteriores como validação do código novo. Próxima ação imediata: concluir casos GNM de defaults/flat e continuar as partes Gen4 restantes; objetivo global ainda INCOMPLETO.

P0 adicional: SubmissionEvidence registra até256 tentativas, fase Validation/Host/Publication/Completed, stream/PC/opcode/target e erro limitado1024bytes, inclusive rejeições anteriores a PrepareDraw. Não confunde retorno de recording renderer com GPU real. Diagnósticos de sucesso, falha de validação, falha do renderer e limites preparados nos testes GNM; sem execução. Dados não usados como fence ou permissão.

P5 completado no caminho de implementação limitado: testes GNM preparados para quatro defaults sem barycentrics e flat MOV P0; outros inputs não consumidos permanecem default para respeitar o contrato de habilitação. Todos os testes desta continuação foram apenas escritos, sem serem executados. Estado de código ainda NÃO VALIDADO.

## Checkpoint da continuação P0–P15 — INCOMPLETO / NÃO VALIDADO

Nesta continuação foram alterados GcnCompiler.h/.cpp, GcnDecoder.h/.cpp, ps4Gnm.h/.cpp e os cinco arquivos de testes/runner listados em build/lumen-integration/continuation-checkpoint.json. Manifesto registra tamanho/SHA256 do estado escrito; não é build nem teste. Histórico/progresso/documentação preservados. Nenhuma alteração Lumen/SharpEmu/shadPS4, instalação, deploy ou commit.

As novas partes são tabelas escalares estáticas com dois buffers+imagem+sampler VS/PS, IB gráfico limitado e PARAM flat/default, com diagnóstico de submissões. Elas NÃO fecham P0–P15. Permanecem: denominador semântico de cobertura completa; SRT dinâmico/recursos gerais; MTBUF/fetch externo/índices/instancing; VOPC/VCC/EXEC e fluxo por lane; interpolação manual/sample/centroid; layouts/formatos/mips/metadados GCN; samplers gerais/storage; depth/MRT/MSAA; waits/releases/CCB/compute; dispatch/LDS/atomics; publicação/coerência/caches/staging persistentes; VideoOut/apresentação/runtime app; GS/tess e fixtures de regressão correspondentes. Nenhum bloqueio externo identificado. O suporte Gen4 total ainda requer implementação substancial.

Retomada: ler docs/PS4_LUMEN_INTEGRATION.md e os fontes, sem tratar os binários antigos como atuais. Próximo caminho de código é MTBUF/fetch externo e index buffers com integração aos dois adaptadores, seguido de EXEC/VCC/fluxo por lane e layout GCN; manter limites de recursos e aliases até implementar coerência. As primeiras tabelas/S_LOAD já estão escritas, não duplicar essa camada nem aumentar o número de user SGPRs. Antes de declarar fechamento, cobrir todo o manifesto Gen4/semântica acordada; depois executar a bateria adiada, corrigir e repetir. Neste checkpoint não foram executados build, CTest, validação SPIR-V/DXIL, GPU, ELF, UWP ou Xbox. Não há evidência nova de funcionamento/regressão aprovada; estado global INCOMPLETO.

## 2026-11-08 — Continuação P3: MTBUF — EM EXECUÇÃO

Consultados decodeInstructionMTBUF/InstControlMTBUF/opcodes Gen4 e MemoryOps/Memory BUFFER_LOAD Kyty. Escrito decoder GCN de TBUFFER_LOAD_FORMAT_X/XY/XYZ/XYZW para FLOAT32 com formato exatamente correspondente à largura, IDXEN/offset DWORD, sem flags/cache/ADDR64/OFFEN. Normalização bit-exata para loads DWORD compartilhados; não há cast de formato GCN para Prospero. Ampliado MUBUF para DWORDX2/X3, definição de grupos, RegisterLimits e clamp do índice antes de stride para IR loads de2/3 DWORDs. Integração segue sem compilação/testes por ordem do usuário. Próxima ação: fixtures próprias cobrindo os quatro componentes e fetch externo.

P3 fetch externo escrito: um S_SWAPPC_B64 inicial do VS com ponteiro imutável e link SGPR par; função limitada a1024DWORDs, S_LOAD inicial, loads MUBUF/MTBUF read-only, waits e retorno S_SETPC no mesmo par. Descoberta respeita comprimento de instrução, relê o intervalo completo e recusa mudança de bytes/mapeamento. Código e tabelas compartilham orçamento4KiB/64ranges; ranges protegem alias com target. PC de retorno opaco não é exposto ao shader; registradores de link entram na conferência RSRC1 por saída scalar_registers. Sem chamadas aninhadas, ALU/branches/export dentro do fetch, S_LOAD intercalado após load vetorial, fetch no PS ou chamadas em fluxo geral.

P3 índices escrito: RasterState recebe snapshot e largura2/4bytes, com tamanho exatamente count*largura. GNM aceita INDEX_TYPE16/32 sem endian swap e DRAW_INDEX_2 DMA sem flags, max_size>=count, count<=65535 e triângulos, leitura40bits/alinhada, orçamento/alias existentes. D3D12 usa upload GENERIC_READ e DrawIndexedInstanced; Vulkan usa buffer INDEX_BUFFER, memória coerente em slot separado e barreira HOST_WRITE→INDEX_READ/vertex-input antes de DrawIndexed. Recursos retidos no escopo já protegido pelo fence/readback dos adaptadores. Mantido draw-auto quando não há índices. Export PS4 sceGnmDrawIndex (NID HlTPoZ-oY7Y) registra builder10DWORDs com validação; sem flags/type extras, nem inferir tipo de índice pelo parâmetro reservado.

Fixtures preparadas, NÃO EXECUTADAS: MTBUF/MUBUF larguras1..4 em VS/PS e negativos de controles; fetch externo com duas V# via tabela e código de retorno próprio; linked DXIL variante24; raster70/71 para geometria/cor alteradas e72/73 para índices16/32 {1,3,2}, escolhendo triângulo oposto ao draw-auto (oracle CPU2048pixels). GNM recording renderer confere snapshots/dados atualizados, retorno/link, alocação SGPR, alias de código/índices e falhas de builder. Ainda não há ELF AMD64 específico de fetch externo ou draw indexado. Fontes PASS apenas expectativas futuras; sem build/CTest/validators/GPU/UWP.

## Checkpoint P3 desta execução — INCOMPLETO / NÃO VALIDADO

Código e fixtures escritos em14arquivos registrados por tamanho/SHA256 em build/lumen-integration/p3-checkpoint.json; isso é inventário estático, não validação. Documentação atualizada em PS4_LUMEN_INTEGRATION/PS4_MUBUF/PS4_GNM/PS4_GPU e neste log. Comandos executados foram leituras/buscas Get-Content/rg e edições/inventários Python UTF-8. Sem compilação, CTest, validação SPIR-V/DXIL, GPU, ELF, UWP, instalações/deploy ou commit. Referências Lumen somente leitura; implementação própria preserva backends e seleção PS5, sem afirmar regressões aprovadas.

P3 ainda não está fechado: formatos packed/normalized/integer gerais, fetch com ALU/instancing/loads intercalados, index offsets/base vertex, restart/endian e draws indiretos gerais continuam pendentes. P0–P15/Gen4 completo permanece INCOMPLETO; não há bloqueio externo identificado. As limitações anteriores de EXEC/VCC/lanes, layouts/formatos/mips, depth/MRT/MSAA, compute/LDS, waits/releases/CCB, coerência/caches persistentes, VideoOut/apresentação e GS/tess permanecem.

Retomada precisa: ler docs/PS4_LUMEN_INTEGRATION.md e os14fontes do manifesto, sem usar binários antigos como evidência. Próximo bloqueio é VOPC/VCC/EXEC com definição/merge por lane na IR existente; manter máscaras/recursos/exports corretamente predicados e não simplesmente aceitar opcodes. Depois layouts GCN e expansão do contrato P3. A primeira chamada de fetch já está materializada pelo FreezeScalarPrelude; não duplicar o parser nem substituir backend. Conferir a bateria preparada (DXIL variante24, raster70..73, unit GNM) apenas quando concluir a integração global exigida, corrigindo e repetindo então. Até esse ponto nenhuma funcionalidade nova ou regressão pode ser anunciada como validada.

## 2026-11-08 — P4: VOPC/VCC e EXEC balanceado — EM EXECUÇÃO

Consultados Compare/Control/Translate/Integer/Vector existentes e opcodes GCN Gen4, após AGENTS e especificação. Escrito decoder das16comparações F32 VOPC para VCC e V_CNDMASK GCN(op0), reutilizando comparações/seleção da IR. S_AND_SAVEEXEC_B64 só com origem VCC e par SGPR alinhado; S_MOV_B64 só restaura EXEC a partir do topo salvo. Flow registra VCC definido por caminho e pilha EXEC até8níveis, compara pilhas em merges, protege as duas palavras salvas, exige destino VGPR previamente definido para escritas sob máscara e recusa branch/export/end enquanto EXEC reduzido. A IR já predica WriteOperand e gateia WriteVectorMask por EXEC; nenhuma alteração da tradução compartilhada PS5 foi necessária. Bounds de descriptors e RSRC1 incluem o par inteiro. Não há CMPX, branches VCC/EXEC, loops ou manipulação arbitrária de EXEC nesse perfil.

Fixtures próprias de seleção VCC, região simples e aninhada escritas; negativos de VCC indefinido, retorno incorreto, saved pair sobrescrito, destino VGPR indefinido, branch interno e export sem restore. Preparados SPIR-V, linked DXIL25..27, raster74..76 com oracle CPU2048pixels que compara lanes alteradas/preservadas e máscaras aninhadas. Nenhum build/validador/CTest/probe foi executado. Próxima ação: integração dos casos GNM e revisão de definição de máscara por caminho.

P4: preparados os três casos GNM sob recording renderer e a recusa de par SGPR fora de RSRC1; testes de todos os16predicados, CFG join com pilhas diferentes e nesting>8 adicionados. Revisão apenas por leitura dos fontes confirmou que o caminho compartilhado já predica VGPRs e VCC e que o restore preserva provenance da máscara. Não equivale a validação compilada ou GPU. Busca inicial com brace expansion inválida no PowerShell foi corrigida com -g antes da leitura; nenhum build/teste foi iniciado.

Checkpoint desta execução: INCOMPLETO/NÃO VALIDADO. Código escrito em GcnDecoder.h/.cpp, GcnCompiler.cpp e ps4Gnm.cpp; fixtures em Ps4GcnShaderTests, Ps4GcnDxilTests, Ps4GnmTests, Ps4RasterTests e runnerGPU; documentação em PS4_LUMEN_INTEGRATION e PS4_GCN_COVERAGE. Ainda faltam comparações I32/U32/64bits, CMPX, máscara B64 geral, else/inversão, branches VCC/EXEC e loops. Próxima ação exata: ampliar o contrato de fluxo por máscara com semântica wave64 e reconvergência usando ConditionRef/CFG/IR existentes, sem remover a proteção de valores inativos; depois avançar layouts GCN. Testes seguem adiados até terminar toda a integração Gen4 solicitada. Sem compilação, validators, CTest, GPU/ELF, UWP, Xbox, instalações ou alterações externas; não existe evidência nova de aprovação. P0–P15 global permanece INCOMPLETO.

## 2026-11-08 — Continuação P4: else e guard EXECZ — EM EXECUÇÃO

Releitura de AGENTS/especificação, checkpoint e tradutores existentes. Confirmado EmitConditionRef: condição EXEC usa voto All/Any de subgrupo e combina as duas metades quando necessário, não branch individual por pixel. Normalização própria GCN acrescenta exclusivamente S_XOR_B64 EXEC,saved_pair,EXEC (complemento dentro da máscara original) e S_CBRANCH_EXECZ forward para else/restore do mesmo frame. Estado de fluxo registra fase then/else junto ao par salvo; permite uma única inversão e compara par+fase em joins. Mismatches/aliases/branches arbitrários continuam recusados. RegisterLimits cobre as duas palavras do par usado pelo XOR; tradução compartilhada PS5 não foi alterada.

Preparadas cinco fixtures: guarded, sem guard, then vazio, else vazio e nested; negativos de par errado, XOR repetido, salto para VALU e else usando frame pai. Sem build ou testes executados por ordem persistente do usuário. Próxima ação: ligar fixtures DXIL/raster/GMN e registrar limitações.

Checkpoint P4 else/EXECZ — INCOMPLETO/NÃO VALIDADO: ligados cinco módulos às variantes linked DXIL28..32 e raster77..81, oracle2048pixels/cena e cinco casos GNM adicionais com recusa RSRC1 insuficiente. Manifesto estático build/lumen-integration/p4-else-checkpoint.json registra8fontes/fixtures por tamanho/SHA256; não é validação. Documentos PS4_LUMEN_INTEGRATION e PS4_GCN_COVERAGE atualizados. Leituras Get-Content/rg, scripts Python de edição/inventário, sem compilar/testar/instalar/deploy/alterar referências. Não houve alteração da tradução compartilhada PS5.

Retomada exata: comparações VOPC I32/U32/64bits/CMPX e operações de máscara/fluxo gerais; preservar predicação por EXEC, provenance dos pares, limites e joins consistentes. Else atual é apenas XOR EXEC,saved,EXEC uma vez; EXECZ atual só salta para else/restore do mesmo frame, forward. Não remover essas recusas para simular suporte geral. Loops, branches VCC, restauração arbitrária e exports parciais ainda ausentes; em seguida layouts GCN continuam como próximo eixo da GPU. Nenhum build, shader validator, CTest, GPU/ELF, UWP ou Xbox foi executado. Suporte global P0–P15 permanece INCOMPLETO e os testes continuam adiados até a integração completa solicitada.

## 2026-11-08 — Continuação P4: VOPC inteiro e CMPX — EM EXECUÇÃO

Lidos AGENTS/especificação, docs e correspondências Gen4. Acrescentadas oito comparações I32 e oito U32 (F/LT/EQ/LE/GT/NE/GE/T) e variantes CMPX F32/I32/U32 no decoder GCN, sem compartilhar números de opcode RDNA2. CMPX é normalizado à comparação base com marca gcn_cmpx_updates_vcc_exec, defaultfalse; o tradutor de comparações atualiza VCC antes de EXEC com gating pelo EXEC de entrada, sem alterar SCC. A marca é exclusiva do frontend GCN; o caminho RDNA2 permanece no ramo anterior. CMPX exige frame EXEC salvo para não permitir exports sob máscara implicitamente perdida. Pilha, guards/else, pares e definições por lane permanecem protegidos.

A alteração de Instruction exige rebuild dos dependentes quando a bateria for liberada; não reutilizar objetos antigos ligados a layout anterior. Sem compilação/testes por ordem do usuário. Próxima ação: fixtures GPU/oracle e negativos específicos de signedness, CMPX e lanes inativas.

Checkpoint P4 inteiro/CMPX — INCOMPLETO/NÃO VALIDADO. Preparadas sete fixtures, emissão dos oito predicados inteiros e negativos de CMPX sem frame; conexões linked DXIL33..39, raster82..88/oracle2048pixels e GNM com15 casos de máscara/comparação. Na leitura do oracle foi corrigida a inicialização de vermelho fora da máscara externa, antes de SAVEEXEC, preservando o VCC da condição externa. Azul após restore depende do VCC produzido por CMPX. Nenhum teste executado.

Comandos/evidências desta continuação: Get-Content e rg sobre decoder, Compare/WriteVectorMask, fixtures, mappings DXIL/SPIR-V e RSRC1; edição Python UTF-8. Um rg com wildcard de diretório falhou com erro123 no Windows; repetido com diretório explícito. Git status confirma muitas alterações pré-existentes, preservadas. Documentos PS4_LUMEN_INTEGRATION e PS4_GCN_COVERAGE atualizados; logs de PASS existem apenas como código de testes preparado, não como evidência de execução.

Próxima ação exata: implementar comparação GCN64bits e ampliar máscaras/branches com definição por lane e reconvergência, consultando as traduções/CFG existentes; continuar layouts/formatos e os demais P antes da bateria final. Rebuild completo obrigatório pela alteração de Instruction; validar PS5 e ambos os backends quando liberado. Sem build, validators, CTest, GPU/ELF, UWP, Xbox, instalações ou deploy nesta continuação. Integração global P0–P15 permanece INCOMPLETA.

## 2026-11-08 — Validação liberada pelo usuário — EM EXECUÇÃO

O usuário autorizou testes agora, substituindo o adiamento anterior. Prioridade: reconstruir dependentes de Instruction e validar a integração existente antes de ampliar ISA. Comando inicial cmake --build build/stage1-desktop --target ps4_gcn_shader_tests ps4_gnm_tests shader_cfg_tests -j6 falhou em gcc sem diagnóstico textual; repetido com C:/msys64/ucrt64/bin no PATH e -j8. Build em andamento, log build/lumen-validation/build-desktop.log. Nada instalado.

Validação em andamento: reconstrução ampla dos três targets executada; ligação de shader_cfg_tests falhou por MapViewOfFile3/UnmapViewOfFile2 ausentes. Corrigida função add_kyty_full_emulator_test em CMakeLists: onecore no ramo WIN32 para ambos toolchains. Regeneração Ninja revelou log truncado e iniciou reconstrução repetida; interrompida apenas essa repetição. build/lumen-validation/commands.json contém comandos CMake atuais; relink-current.py recompila o teste e liga os objetos próprios de cada target já reconstruídos, sem trocar objetos entre targets. Logs compile/link no mesmo diretório.

Primeiro shader test falhou; diagnóstico source_location adicionado identificou linha642: duas instruções MOV das fixtures SMRD não incluíam opcode1. Corrigidos 0x7e000020->0x7e000220 e 0x7e0a0020->0x7e0a0220. Nova execução ps4_gcn_shader_tests build/lumen-validation/fixtures saiu0; shaders.log confirma validadores e casos. ps4_gnm_tests --unit-only saiu0 (gnm-unit.log), incluindo tabelas/múltiplos recursos/sampler. CTest shader_cfg aprovado1/1 em13,42s após ligação corrigida (ps5-ctest.log); tentativa anterior antes da ligação ficou Not Run, sem evidência de aprovação. Runner tests/run-ps4-gcn-gpu.ps1 iniciado com Mesa-UWP existente e VulkanSDK1.4.357.0; gpu.log pendente.

Testes desktop/GPU aprovados nesta rodada: CTest regex ^(ps4_gnm_.*|ps4_gcn_shader|shader_cfg)$ passou10/10 em20,40s. Runner GPU passou:80 validações DXIL SM6.4 (40pares),89 cenas por backend e1mista por backend =270 readbacks,2048pixels/cena. D3D12 RX6600M, WARP e Vulkan RX6600M; zero erros reportados pelas camadas. Evidências gpu.log,ctest.log,shaders.log,gnm-unit.log e results.json com SHA256. Implementação dos perfis anteriores está agora validada nos limites das fixtures; não significa suporte Gen4 geral. Referência de opcodes64bits consultada em Lumen source/gen4/src/shader_recompiler/frontend/opcodes.h, leitura apenas, sem cópia de código nesta rodada. Build UWP em andamento via src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp, log uwp.log.

Incremento desta rodada CONCLUÍDO: desbloqueio da ligação de testes Windows e validação dos perfis integrados. Build UWP/D3D12 completo saiu0 (96etapas, uwp.log); auditoria explícita da função Test-Imports do script existente retornouTrue para4binários do layout (uwp-imports.log). Nenhuma instalação/deploy ou execução UWP/Xbox. Documentos atualizados: especificação, PS4_COMPATIBILITY/GPU/GNM/GCN_COVERAGE e checkpoint Lumen; resultados.json registra contagens e hashes. Arquivos de código alterados nesta rodada: CMakeLists.txt (onecore WIN32), tests/Ps4GcnShaderTests.cpp (dois MOVs SMRD corretos e diagnóstico com linha). Referências usadas apenas em leitura.

Limites: P0–P15/Gen4 completo permanece INCOMPLETO. Não há ELF específico de sampling/fetch externo/draw indexado, nem readbacks numéricos de todas as ALU novas; as fixtures exercitadas não equivalem a jogos. PS5 verificado por shader_cfg, não gameplay. Problema de registros truncados do Ninja não foi corrigido estruturalmente; comandos CMake atuais e workaround de recompilação/ligação estão preservados em build/lumen-validation/relink-current.py. Todos os dependentes do novo layout Instruction foram reconstruídos antes das aprovações.

Próxima ação exata: comparações VOPC I64/U64/CMPX64 (Gen4 bases160/224), validar definição e limites de AMBAS as palavras de fontes em pares no decoder, imutabilidade no compilador e RSRC1 no GNM; reutilizar tradução64 existente, adicionar fixtures que diferenciem palavras altas/baixas, signedness e lanes inativas e executar shader/GPU/PS5/UWP. Não aceitar apenas opcode normalizado com checagem de um registrador. Depois ampliar fluxo/máscaras e layouts GCN.


## 2026-11-09 — Desvios VCC/EXEC e operações B64

O decoder GCN aceita desvios para frente S_CBRANCH_VCCZ, VCCNZ, EXECZ e EXECNZ. O CFG compartilhado avalia a condição por subgrupo. O frontend exige VCC definido antes da leitura e valida o destino, a pilha de EXEC e a definição de registradores em todos os caminhos de entrada. VGPRs escritos sob EXEC reduzido ainda precisam de valor anterior para as lanes inativas.

S_AND_B64, S_OR_B64, S_XOR_B64, S_ANDN2_B64 e S_ORN2_B64 aceitam pares alinhados de SGPRs. O decoder verifica ambas as palavras, o compilador protege as duas palavras de descritores imutáveis e o GNM verifica a alocação RSRC1. O tradutor compartilhado calcula SCC a partir do resultado de 64 bits. O XOR canônico para alternar entre then e else continua sujeito às regras do frame salvo.

O objetivo de fluxo e máscaras gerais permanece incompleto. Escritas livres em EXEC/VCC, operandos especiais nas operações B64, loops e as famílias restantes de CMP/CMPX exigem implementação e validação próprias. O próximo passo é representar a proveniência das máscaras e as definições por lane nos pontos de junção e nas arestas de retorno.

Em validação. Os opcodes SOPP 6–9 e SOP2 15/17/19/21/23 foram conferidos no Gen4 local. O script local build/ps4-mask-branches-build.py extrai comandos atuais do Ninja para recompilar decoder, compilador, GNM e teste alterados, e religar os três executáveis desktop. Logs em build/ps4-mask-branches. Build UWP disparado com src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp.

CTest aprovou 10/10 em 36,09 s, incluindo os casos novos de SPIR-V, as fixtures GNM no desktop/Vulkan e a regressão PS5 shader_cfg. Build UWP/D3D12 terminou com código 0. Os casos novos de desvios e B64 ainda aguardam readback próprio em D3D12/Vulkan. Logs em build/ps4-mask-branches/ctest.log e uwp.log. Incremento de frontend validado. Objetivo completo INCOMPLETO.


## 2026-11-09 — Arestas de retorno no CFG GCN

GcnDecoder.cpp agora aceita offsets de salto com sinal e valida definições de registradores, exports e pilha EXEC por fila de trabalho até convergir, incluindo arestas de retorno. O estado inicial participa da junção na entrada. Uma definição em iteração posterior não valida uma leitura da primeira iteração. A escrita mascarada continua exigindo valor prévio para as lanes inativas. GcnCompiler.cpp recusa saltos para o preâmbulo escalar especializado.

Ps4GcnShaderTests.cpp valida SPIR-V para um laço escalar de três iterações e rejeita a variante que lê o contador sem inicialização. Os binários desktop foram recompilados e religados com os comandos do Ninja extraídos por build/ps4-gcn-loops-build.py. CTest aprovou 10/10 em 30,77 s, incluindo GNM/Vulkan e shader_cfg PS5. O build UWP/D3D12 terminou com código zero via src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp. Logs em build/ps4-gcn-loops. O laço novo foi validado em SPIR-V. Readback dedicado e execução em Xbox continuam pendentes.

O objetivo completo permanece INCOMPLETO. Próxima ação: testar laços com EXEC variável e preservação de VGPR por readback, depois modelar a proveniência de escritas livres em EXEC/VCC e ampliar CMP/CMPX a partir dos opcodes Gen4. O teste atual comprova o laço escalar, com cobertura pendente para CFG irreducível e divergência com múltiplas saídas.

## 2026-11-09 — CMPX de 64 bits e laço divergente

O decoder normaliza VOPC I64/U64 e CMPX correspondentes a partir dos opcodes Gen4. Valida ambas as palavras da fonte e o GNM aplica RSRC1 ao par inteiro. MOV_B64 copia pares SGPR e VCC, e pode ler EXEC completo. Escritas em EXEC continuam sujeitas à restauração do frame salvo. Branches por SCC podem fechar um laço dentro do frame se a pilha coincidir na junção. A fixture CMPX exercita a ida e volta de VCC por SGPR e duas somas sob EXEC reduzido, com a cor original preservada fora da máscara.

Build UWP/D3D12 via src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp retornou zero. Build desktop e testes em andamento. Logs em build/ps4-gcn-divergence.


## 2026-11-09 — VOPC I64/U64 e laço sob EXEC reduzido

GcnDecoder.cpp adiciona os oito predicados VOPC de I64 e U64 e as variantes CMPX. A codificação foi conferida no Gen4 local e a tradução usa as operações de comparação existentes da IR. As duas palavras dos operandos precisam estar definidas e caber na alocação RSRC1. S_MOV_B64 copia pares SGPR/VCC e lê EXEC completo. O destino EXEC continua restrito ao par do frame salvo. Branches SCC podem fechar laços dentro do frame com a mesma pilha nos pontos de junção.

A fixture CMPX faz a cópia VCC → SGPR → VCC, executa duas somas sob EXEC reduzido, reconverge e verifica tanto a cor acumulada das lanes ativas quanto a cor preservada das demais. As variantes 87 e 88 cobrem CMPX I64 e U64. CTest passou 10/10 em 25,20 s, incluindo shader_cfg PS5. O script tests/run-ps4-gcn-gpu.ps1 passou em D3D12 hardware, WARP e Vulkan com 2048 pixels por cena e zero erros reportados pelas camadas. O build UWP/D3D12 passou. Logs de compilação, CTest, UWP, SPIR-V e GPU em build/ps4-gcn-divergence. A execução foi no desktop.

Objetivo completo INCOMPLETO. Permanecem pendentes escritas gerais de EXEC, operações sobre metades de EXEC/VCC, tipos e formatos restantes de CMP/CMPX e CFG divergente com múltiplas saídas. A próxima extensão é modelar a proveniência de máscaras salvas fora do padrão AND_SAVEEXEC e testar restauração em junções com múltiplas saídas.

## 2026-11-09 — Metades de máscara, F64 e duas saídas

Incremento em validação. GcnDecoder.cpp liga MOV/NOT e a ALU escalar já suportada às metades de EXEC/VCC. Operações B64 aceitam pares de máscara. O fluxo mantém definições distintas para VCC_LO e VCC_HI e exige um frame para alterar EXEC. Foram acrescentados os 16 predicados VOPC F64 e as variantes CMPX, usando a tabela Gen4 local. O teste de duas saídas exige que ambas restaurem EXEC antes do export. A fixture de readback CMPX também percorre as quatro metades via MOV_B32.

Build UWP/D3D12 terminou com código zero. A reconstrução desktop usa build/ps4-gcn-mask-halves-build.py, com comandos extraídos do Ninja. Evidências em build/ps4-gcn-mask-halves.

Primeira rodada aprovada. CTest 10/10 em 15,87 s e build UWP/D3D12 concluído. Runner SPIR-V/DXIL passou em D3D12, WARP e Vulkan com zero erros de validação. O readback cobre as quatro metades por MOV_B32 sob divergência. F64 CMPX e o CFG de duas saídas foram exercitados no validador SPIR-V, incluindo recusa da saída sem restauração. Logs build/ps4-gcn-mask-halves/ctest.log, gpu.log e uwp.log.

Segunda rodada em validação. Acrescentados os seis predicados I16/U16 de VOPC, CLASS_F32 CMP/CMPX, ANDN2/ORN2_B32 e NAND/NOR/XNOR_B32/B64. Fixtures de compilação SPIR-V para as novas comparações. Logs build/ps4-gcn-narrow-compare.


Em 2026-11-09, o frontend GCN passou a aceitar MOV, NOT e ALU escalar sobre as metades de EXEC/VCC, além de operações lógicas B64 com pares de máscara. A análise distingue as duas metades de VCC e exige frame salvo para alterar EXEC. Foram adicionados VOPC F64 CMP/CMPX, I16/U16 e CLASS_F32, além de testes de CFG com duas saídas restauradas e rejeição de saída sem restauração. As quatro metades passaram por readback D3D12, WARP e Vulkan na rodada ps4-gcn-mask-halves. O teste de F64 e o CFG de duas saídas passaram em SPIR-V. A rodada ps4-gcn-narrow-compare passou em CTest e build UWP/D3D12. Logs nos respectivos diretórios build.

Meta ativa e incompleta. Próxima ação: implementar VOPC F16 e CLASS_F64 com checagem de NaN, zero e denormais. Depois cobrir os formatos VOP3 e sinalizantes, escritas de EXEC fora do padrão de frame e readback que force cada uma das duas saídas do CFG. Escritas de EXEC atualmente exigem frame e a restauração permanece vinculada ao par salvo.

## 2026-11-09 — F16 VOPC e classificação F64

GcnDecoder.cpp liga os oito predicados F16 da tabela VOPC Gen4 à comparação em F32 após conversão de cada metade baixa. CLASS_F64 e CMPX_CLASS_F64 usam os bits do par de entrada para distinguir as dez classes sem aritmética de ponto flutuante. O classificador lê a máscara de classes como um DWORD e GNM verifica o par somente no operando F64. Referência: opcodes.h e format.cpp do Gen4 local.

CTest 10/10 em 14,56 s, incluindo shader_cfg, e build UWP/D3D12 com código zero. O binário numérico separado, gerado com build/ps4-gcn-fp-numeric.py para preservar a execução CTest em andamento, valida os dez padrões F64 com máscara correta e complementar e três casos F16 (zero com sinal, subnormal e NaN). O validador SPIR-V e a conversão DXIL passaram. Readback em andamento via tests/run-ps4-gcn-gpu.ps1. Logs em build/ps4-gcn-fp-classes.


A variante numérica 84 passou nos 2048 pixels em D3D12, WARP e Vulkan, com zero erros das camadas. Testes GPU incluíram CLASS_F64 com os dez padrões IEEE e as máscaras complementares e F16 com zero de sinal oposto, subnormal positivo e NaN. CMPX_CLASS_F64 foi coberto separadamente no validador SPIR-V. Build UWP/D3D12 e regressão shader_cfg passaram. Sem execução Xbox. Evidências em build/ps4-gcn-fp-classes.

Meta ativa. Próxima ação: compartilhar o mapa de predicados entre VOPC e VOP3, com destinos explícitos em par SGPR e controle de modificadores. Comparações sinalizantes, escritas de EXEC fora do padrão de frame e readback de cada saída divergente permanecem pendentes.

## 2026-11-09 — CMP/CMPX VOP3 com destino explícito

GcnDecoder.cpp compartilha o mapa de predicados VOPC/VOP3 conferido em decode.cpp e opcodes.h do Gen4. VOP3 normaliza para a operação vetorial existente e valida destino em par escalar, largura de cada fonte, campo de fonte não usada e modificadores. GcnCompiler.cpp protege ambas as palavras do destino contra escrita em descritores, e ps4Gnm.cpp aplica RSRC1 ao destino inteiro. Compare.cpp grava o destino selecionado e, em CMPX GCN, também restringe EXEC pela máscara de entrada. O caso VOPC mantém VCC como destino explícito do decoder.

Build UWP passou. CTest das nove regressões selecionadas (GNM e shader_cfg) passou em 26,43 s. O teste de shader atualizado é compilado separadamente via build/ps4-gcn-vop3-fixture.py. A primeira compilação encontrou colisão de nomes na fixture. A primeira execução revelou a falta do bit de seleção VGPR na segunda fonte VOP3 e foi corrigida. Readback em andamento. Logs em build/ps4-gcn-vop3-compare.


CMP/CMPX VOP3 foi validado com readback da variante 84 nos 2048 pixels em cada backend D3D12, WARP e Vulkan. O caso CMPX grava s[20:21] e reduz EXEC. O caso CMP verifica o modificador ABS, a preservação de VCC até a cópia e o valor observado após copiar o par. SPIR-V, DXIL, nove testes GNM/PS5 e build UWP/D3D12 passaram. Evidências em build/ps4-gcn-vop3-compare. Sem execução Xbox.

A meta permanece ativa. Próxima ação: mapear variantes sinalizantes de F32/F64 e avaliar os efeitos observáveis de exceção GCN, além de completar seletores e fontes dos formatos restantes. Escritas gerais de EXEC fora do frame salvo e readback que force as duas saídas do CFG continuam pendentes.


## 2026-11-09 — Readback das duas saídas do CFG divergente

Ps4GcnShaderTests gera dois módulos. Ambos executam uma escrita VALU sob EXEC reduzido. Depois, S_AND_B64 prepara EXECZ ou EXECNZ a partir do par salvo e de uma máscara conhecida. Isso força uma saída por cena independentemente do tamanho de wave do host. Cada saída restaura EXEC, escreve um valor azul distinto e exporta. O vermelho distingue as lanes que executaram a escrita das lanes que preservaram o VGPR. O caso negativo que elimina a segunda restauração continua sendo recusado pelo decoder.

Ps4GcnDxilTests, Ps4RasterTests e run-ps4-gcn-gpu.ps1 incorporam os dois módulos. Readback passou nas duas saídas em D3D12, WARP e Vulkan, com 2048 pixels por caso, total de 12288 pixels novos e zero erros de validação. SPIR-V e DXIL passaram. CTest das nove regressões GNM/PS5 passou em 4,91 s. Os fontes do runtime e do UWP são os mesmos da última rodada com build aprovado. Logs em build/ps4-gcn-multi-exit. Sem execução Xbox.

Meta ativa. Próxima ação: permitir o padrão de salvar EXEC com MOV em um par SGPR, rastrear a proveniência nas junções e validar restauração após escritas diretas. Comparações sinalizantes e seletores e operandos restantes também continuam pendentes.


## 2026-11-09 — EXEC salvo por MOV e escritas diretas

GcnDecoder.cpp rastreia pares SGPR com uma cópia de EXEC de entrada, inclusive cópias B64 entre pares. Uma escrita em qualquer palavra invalida a proveniência. Nas junções, somente pares comprovados em todos os caminhos sobrevivem. A presença de uma cópia permite alterar EXEC por B64 ou por metades sem AND_SAVEEXEC. Antes de exportar, todo caminho com EXEC alterado precisa restaurá-lo de um par comprovado. O controle de frames existente continua válido para regiões aninhadas.

As duas fixtures de CFG usam MOV para salvar EXEC, AND para selecionar lanes e MOV para copiar o par antes da restauração em cada saída. Uma cena também limpa EXEC_LO/HI separadamente. O caso de clobber de uma palavra antes da cópia é recusado. A primeira execução CTest compilou a fixture antes do ajuste na posição do clobber e falhou na checagem do diagnóstico. A recompilação focada e o CTest final passaram 10/10 em 9,01 s, incluindo shader_cfg. Build UWP/D3D12 passou. Readback passou nas duas saídas em D3D12, WARP e Vulkan, 12288 pixels no total e zero erros de validação. Logs em build/ps4-gcn-direct-exec. Sem execução Xbox.

Meta ativa. Ainda falta aceitar restaurações comprovadas com metades salvas e restauradas separadamente, assim como escritas cuja restauração não passe por um par SGPR de entrada. Próximo incremento: rastrear a proveniência separada de EXEC_LO/HI e validar os joins que perdem somente uma metade. Comparações sinalizantes e seletores e operandos restantes continuam pendentes.


## 2026-11-09 — Restauração independente de EXEC_LO e EXEC_HI

GcnDecoder.cpp acompanha a proveniência de cada metade por SGPR. MOV_B32 salva, copia e restaura uma metade, MOV_B64 transfere a proveniência de ambas. Escritas parciais em SGPR invalidam a palavra atingida. Junções intersectam a proveniência e conservam qualquer metade que ainda precise ser restaurada. A largura da máscara de saída de CMP é 64 bits mesmo com operandos de 32 bits.

A fixture de duas saídas cobre o par B64 e a restauração por dois MOV_B32, com um caso negativo em que só um caminho sobrescreve a metade alta. CTest final 10/10 em 5,94 s, incluindo shader_cfg. Build UWP/D3D12 aprovado. Readback D3D12, WARP e Vulkan passou com 12288 pixels nas duas saídas e zero erros das camadas. Após a correção da largura de escrita CMP, os 48 módulos regenerados coincidiram por SHA-256 com os usados no readback. A primeira execução manual para conferir hashes falhou por falta do PATH MSYS2 e foi repetida com o ambiente correto. Logs em build/ps4-gcn-exec-halves. Sem execução Xbox.

Meta ativa. Próxima ação: ampliar os formatos de operandos de comparação e tratar CMP sinalizante com semântica observável de exceção. Restauração por expressões equivalentes e exports com EXEC modificado ainda exigem modelagem adicional.

## 2026-11-09 — Fontes de 64 bits de CMP/CMPX

GcnDecoder.cpp aceita constantes inline e pares EXEC/VCC nas fontes de 64 bits. VOPC também aceita literal de um DWORD, enquanto VOP3 conserva a regra sem extensão literal. A leitura de um par VCC exige ambas as metades definidas. Compare.cpp trata a expansão de literal de CLASS_F64 da mesma forma que os outros predicados F64. A referência é a regra de operandos por tipo da seção 6.2.1 do guia AMD GCN3 e o decoder Gen4 local.

CTest 10/10 em 7,20 s e build UWP/D3D12 aprovados. Primeiro readback das constantes passou em D3D12, WARP e Vulkan. A checagem de timestamps mostrou que o teste de fontes EXEC/VCC chegou após a compilação da primeira fixture. Recompilação isolada via build/ps4-gcn-wide-operands-fixture.py e novo readback em andamento. Logs em build/ps4-gcn-wide-operands.


A rodada final incluiu VOP3 U64 com fontes EXEC e VCC, além de literais VOPC F64/I64/U64 e CLASS_F64, e inline F64/I64. A variante 84 passou em D3D12, WARP e Vulkan, 2048 pixels por backend. Validadores SPIR-V/DXIL e camadas GPU passaram. CTest final 10/10 inclui a fixture atual e shader_cfg PS5. Build UWP/D3D12 aprovado. Sem execução Xbox. Evidências em build/ps4-gcn-wide-operands.

Meta ativa. Próxima ação: tratar fontes especiais de comparação (SCC, EXECZ e VCCZ) com definição e voto corretos e ampliar formatos SDWA/DPP conforme a geração. Comparações sinalizantes e exports com EXEC alterado ainda exigem modelagem.


## 2026-11-09 — Fontes de condição em CMP/CMPX

GcnDecoder.cpp aceita SCC, EXECZ e VCCZ em VOPC e VOP3. VOP3 aplica a restrição da fonte escalar compartilhada. SCC precisa estar definido e VCCZ exige as duas metades de VCC. A tradução reutiliza a leitura escalar existente, incluindo extensão com zeros para fontes de 64 bits. A fixture cobre SCC 0/1, VCCZ 0/1 e EXECZ com EXEC restaurado, e casos negativos de SCC e VCCZ indefinidos.

CTest passou 10/10 em 6,61 s, incluindo shader_cfg do PS5. O build UWP/D3D12 passou. A variante 84 passou nos 2048 pixels em D3D12, WARP e Vulkan, com validadores SPIR-V/DXIL aprovados. Logs em build/ps4-gcn-condition-operands. Sem execução Xbox.

Meta ativa. Próxima ação: validar as regras SDWA do perfil GCN, incluindo seleção e extensão de subpalavras e distinções de DPP entre gerações. Comparações sinalizantes e exports com EXEC alterado continuam pendentes.
