# PS4 — shaders e integração GNM

Atualização 2026-11-08: [IMAGE_LOAD RGBA8 e recursos VS/PS simultâneos](PS4_TEXTURES.md) validados: até dois buffers e uma textura por estágio, ELF com seis payloads, 25/25 testes, Vulkan/D3D12/WARP e build UWP aprovados. Samplers/filtragem, demais formatos/tiling e fase 4 geral permanecem incompletos.


Atualização 2026-11-08: [MUBUF indexado, dois buffers e vertex fetch](PS4_MUBUF.md) implementados e validados no contrato limitado: 24/24 testes, ELF guest com índice/dados mutáveis, Vulkan/D3D12/WARP e build UWP aprovados. Texturas/MIMG, buffers simultâneos VS+PS e a fase 4 geral continuam incompletos.


Atualização 2026-11-08: [interpolação e cobertura GCN](PS4_GCN_COVERAGE.md): mapa smooth PARAM e interpolação linear central integrados ao GNM; sete VOP1 adicionais exercitadas com buffer GPU real. 23/23 testes, Vulkan/D3D12/WARP e build UWP aprovados nos níveis documentados. Todas as famílias GCN e a fase 4 continuam incompletas.


Atualização 2026-11-08: [descriptor e SMRD com buffer GPU real](PS4_GCN_BUFFERS.md) validados no contrato inicial de um buffer read-only no PS, com offset imediato. ELF guest altera os dados entre submissões; 23/23 testes, probes Vulkan/D3D12/WARP e build UWP aprovados nos níveis documentados. P1/P2 gerais e fase 4 continuam parciais.

Incremento validado em 2026-11-08: [transferências no GNM](PS4_GNM_TRANSFERS.md) integra WRITE_DATA/DMA_DATA immediate-fill aos draws, limitado aos alvos atuais e com staging síncrono. ELF guest e 22/22 testes aprovados; Vulkan/D3D12/WARP e build UWP verificados nos níveis descritos no relatório. Recursos gerais e apresentação continuam pendentes.

Incremento de 2026-11-08. O runtime diagnóstico PS4 registra cinco exports reais de `libSceGnmDriver`: `sceGnmSetVsShader`, `sceGnmSetPsShader`, `sceGnmDrawIndexAuto`, `sceGnmSubmitCommandBuffers` e `sceGnmSubmitDone`. A implementação própria está em `src/libs/ps4Gnm.*`; o runtime seleciona o renderer Vulkan ou D3D12 conforme o backend do build. A interface normal continua bloqueando jogos PS4.

## Incremento de shaders gerais — 2026-11-08

O frontend aceita uma interface explícita de até quatro parâmetros completos (`PARAM0..3`) no VS e `v_interp_p1_f32`/`v_interp_p2_f32` no PS. A tradução reutiliza os exports de atributos e interpolantes nativos da IR, do emissor SPIR-V e do linker DXIL existentes. Não houve alteração da tradução compartilhada PS5.

O decoder exige um POS0 e todos os PARAM declarados em cada caminho de saída, sem duplicações por caminho. PARAMs são não comprimidos, máscara completa e done=0; POS0 preserva done=1. O contrato P1/P2 atual exige instruções adjacentes, mesmo destino/atributo/canal e fontes v0/v1 do par perspectiva central. Recusa branch que entra em P2, P2 isolado, atributo fora do contrato, escrita em v0/v1 e reaproveitamento observável do intermediário P1. ALU após POS0 done continua limitada conforme o subconjunto anterior.

O GNM valida `SPI_VS_OUT_CONFIG`, `SPI_PS_IN_CONTROL`, `SPI_PS_INPUT_ENA/ADDR` e `SPI_PS_INPUT_CNTL_0..3`: perspectiva central habilitada por bit 1, mapa de parâmetros identity/smooth, sem default/flat ou flags extras. O RSRC1 precisa comportar os registradores usados. Contagem, mapa ou shader incompatíveis são recusados antes de qualquer chamada ao renderer, preservando estado e imagem.

Também foram adicionados VOP3 F32 add/sub/subrev/mul/min/max, MAD e FMA; abs e negação por fonte, omod e clamp de saída. O decoder usa os campos GCN (opcode em bits 17..25; clamp em bit 11), distintos de RDNA2. Recusa op_sel/reservados, extensão literal VOP3, múltiplas fontes SGPR diferentes e operandos não utilizados não zerados. Min/max F32 também estão disponíveis em VOP2. A IR já implementava essas operações e modificadores; nenhuma segunda ALU foi criada.

As fixtures próprias agora produzem triângulo com gradiente, perspectiva com W diferente entre vértices mantendo a mesma cobertura, e PS com `clamp(1-2*abs(red))`, `(green*.5+.5)*.5` e min/max no azul. As referências CPU usam coordenadas de pixel e divisão perspectiva independentes do código shader. O caso de quatro slots exporta cores distintas em PARAM3 e o PS lê somente esse slot; isso verifica a ligação com inputs não consecutivos. A tolerância é um nível UNorm; os testes não certificam todos os casos NaN, denormal ou arredondamento de MAD/FMA.

Evidências: `build/stage4-shaders/`: doze fixtures SPIR-V (e dois checks adicionais da interface de quatro slots), dezoito objetos DXIL, dez cenas nos três renderizadores, e ELF guest verificando todos os 2048 pixels. O resultado final de regressão está registrado em PROGRESS.md. O caso real de GPU também verifica quatro slots com cores distintas e consumo apenas de PARAM3. Isso ainda não constitui shaders gerais completos de jogos.

## Arquivos do incremento

- `src/graphics/shader/recompiler/frontend/decode/GcnDecoder.{h,cpp}`: decodificação VOP3/VINTRP, exports PARAM, validação por caminho e fixtures próprias.
- `src/graphics/shader/recompiler/GcnCompiler.{h,cpp}`: interface explícita de parâmetros para a tradução/IR existente.
- `src/libs/ps4Gnm.cpp`: validação dos registradores SPI e propagação da interface à compilação e ao limite de registradores.
- `tests/Ps4GcnShaderTests.cpp`, `Ps4GcnDxilTests.cpp` e `Ps4RasterTests.cpp`: bytecode inválido, validadores e referência independente para os novos resultados GPU.
- `tests/Ps4GnmTestFixture.h` e `Ps4GnmTests.cpp`: ELF AMD64 de gradiente/ALU, conferência completa do framebuffer, rollback de estado e controles inválidos.
- `tests/run-ps4-gcn-gpu.ps1` e `CMakeLists.txt`: novos artefatos exigidos pelo probe e testes CTest de perspectiva/ALU guest.
- `PROGRESS.md`, arquivo de objetivo e documentos de compatibilidade/GPU/GCN: estado, evidências e próxima ação. O lançamento normal de jogos PS4 permanece bloqueado.

## Verificação final deste incremento

| Verificação | Resultado / log em `build/stage4-shaders/` |
|---|---|
| Build desktop do núcleo e raster Vulkan | PASS — `build-desktop-final.log`, `build-raster-final.log` |
| Regressão selecionada PS4/PS5 | **21/21** — `ctest-final.log`, `ctest-details.log` |
| 12 fixtures SPIR-V e checks VS de quatro slots / PS de input não consecutivo | PASS — `shaders-final.log` |
| 18 objetos DXIL e 30 imagens GPU, 2048 pixels RGBA por imagem | PASS — `gpu-final.log`; D3D12 debug / Vulkan validation: zero erros |
| Comparação dos readbacks RGB | D3D12 Radeon = Vulkan; WARP no máximo 1 UNorm — `readback-comparison.log` |
| ELF AMD64 → GNM → GPU, gradiente com perspectiva e ALU | PASS — `ctest-details.log` |
| Build completo UWP/D3D12 e layout | PASS — `build-uwp-final.log` |
| Imports UWP contra WindowsApp.lib | PASS — `audit-uwp-final.log` |

Os testes PS5 cobrem tradução/CFG, proveniência e rastreamento/materialização de recursos, além dos demais contratos selecionados. Não houve teste de gameplay PS5, execução AppContainer ou Xbox. A missão de shaders gerais continua parcial; este resultado fecha o incremento de interpolação/ALU.

## Shaders

O frontend GCN passou a aceitar SGPRs, literais compartilhados, ALU escalar de 32 bits (mov, not, add/sub, min/max, seleção por SCC, operações lógicas, shifts e multiplicação), comparações escalares assinadas/sem sinal, `s_movk_i32`, NOP e branches para frente por SCC. As operações vetoriais incluem sub reverso, OR e XOR, além do subconjunto anterior. O decoder verifica limites de instrução, destinos de branch e definição dos registradores em todos os caminhos que alcançam cada uso. Cada saída alcançável exige um export completo; loops, código inalcançável e ALU após export done são recusados.

Até 16 user SGPRs são capturados dos registradores SH e incorporados como constantes antes da tradução. O caminho continua GCN → IR compartilhada → SPIR-V; no D3D12 utiliza o `DxilCompiler` de produção e a distribuição local do Mesa-UWP. Não encaminha GCN ao decoder RDNA2.

## Contrato GNM atual

- DCBs type-3 sem predicação: NOP opaco, SET_SH_REG, SET_CONTEXT_REG, SET_UCONFIG_REG, NUM_INSTANCES=1 e DRAW_INDEX_AUTO. CCBs e comandos desconhecidos são recusados.
- Limites por lote: 16 DCBs, 4 MiB de comandos, 64 draws e 16 MiB de alvos distintos; shaders com até 4096 palavras. Registradores não implementados são recusados. Alocações VGPR/SGPR declaradas no RSRC1 são conferidas.
- Triangle list, uma instância, contagem múltipla de três, VS por vertex ID e PS uniforme. Alvo RGBA8 UNorm linear-aligned com pitch múltiplo de 64, até 1024×1024. Viewport completo, scissor, máscara RGBA e blend limitado. Depth/stencil/cull desativados.
- Snapshot e validação de todos os comandos, shaders, estados e destinos antes de executar a GPU. Os bytes anteriores do alvo alimentam o primeiro draw; draws seguintes recebem o resultado anterior. A memória guest só é atualizada depois que todos os draws terminam. Falhas de validação não executam trabalho GPU.
- Uma lease mantém as montagens de memória do perfil válidas durante snapshots, fences e cópia final. Recursos GPU permanecem vivos até conclusão. O protocolo atual é síncrono; SubmitDone não simula eventos ou filas assíncronas.

## Testes

`Ps4GnmTests.cpp` verifica validação, estado persistente, transação, especialização por user SGPR e duração da lease com um renderer de teste. A fixture ELF AMD64 própria em `Ps4GnmTestFixture.h` chama os cinco imports pelo runtime, constrói os comandos em memória guest e verifica pixels cobertos/descobertos após duas submissões e a rejeição de um lote inválido.

Os probes gráficos cobrem seis cenas, verificando todos os 2048 pixels de cada imagem: triângulo, scissor, blend/máscara, preservação de imagem anterior e duas decisões de branch escalar. Logs deste ciclo: `build/stage4-gnm/`.

## Resultado verificado — 2026-11-08

| Verificação | Resultado / evidência |
|---|---|
| Regressão selecionada CPU/HLE, loader, arquivos, shaders e recursos PS5, GPU e GNM | 19/19, `build/stage4-gnm/ctest-final.log` |
| ELF AMD64 → imports → PM4 → GCN → Vulkan → framebuffer guest | PASS; status `0x600d474e`, 2 submissões, 2 draws e 1 rejeição; `gnm-guest.log` e `guest/gnm-guest.ppm` |
| Seis cenas em Radeon RX 6600M D3D12, WARP e Vulkan | Todos os pixels conferidos; zero erros D3D12 debug/Vulkan validation; `gpu.log` |
| Mesa-UWP / validação SDK DXIL | 10 objetos VS/PS aprovados, SM 6.4; `gpu.log` |
| Build completo UWP/D3D12 e layout | PASS; `build-uwp-final.log` |
| Imports do layout contra WindowsApp.lib | PASS; `audit-uwp-final.log` |

A primeira execução guest falhou com `STATUS_BAD_STACK` durante a submissão feita na pilha guest. A submissão agora é executada em uma thread nativa criada antes da entrada no ELF; a API aguarda seu resultado. Isso mantém a compilação e as chamadas ao driver na pilha do host. Finish drena o trabalho e aguarda a thread antes de liberar renderer e callbacks. O teste guest passou após essa alteração. Não foi executado aplicativo instalado, AppContainer ou Xbox.

## Referências locais

A implementação é própria. Foram conferidos os encodings/opcodes e registradores com `shadPS4-main/src/shader_recompiler/frontend/` e `src/video_core/amdgpu/`, e as identidades/NIDs e builders com `src/core/libraries/gnmdriver/gnmdriver.cpp`. O registro de exports Gen4 em `sharpemu-main/src/SharpEmu.GnmMode/GnmExportRegistry.cs` confirma a separação GNM/AGC. Não houve importação de um runtime independente; os shaders usam a tradução/IR e os adaptadores host do Kyty.

## Procedência do incremento de interpolação/ALU

Código e fixtures próprios; consulta de formatos e semântica aos arquivos locais abaixo (somente leitura, GPL-2.0-or-later). Não houve transposição de funções das referências. A estrutura de normalização GCN do SharpEmu foi consultada como comparação arquitetural; a implementação usa as APIs existentes do Kyty.

| Referência shadPS4 | SHA-256 |
|---|---|
| `src/shader_recompiler/frontend/instruction.h` | `1dadfedc7a9ae73ea926ee0b2e4c6c5c77993048891cee630596e1ac3efb25f4` |
| `src/shader_recompiler/frontend/opcodes.h` | `e79eef6518fb95d6951136c0f4676a456ee45c2403108d84bef4ea3164e30226` |
| `src/video_core/amdgpu/regs_shader.h` | `e4d782fbf35d30a0a5e83fa38170e286e6dd494faaac4f79fc1d4833648a0bdb` |

Reprodução com as dependências já configuradas (nenhuma instalação):

```powershell
cmake --build build/stage1-desktop --target ps4_gcn_shader_tests ps4_gnm_tests ps4_raster_vulkan_tests
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-shaders/fixtures
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-shaders/guest --interpolation
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-shaders/guest-alu --alu
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-shaders/fixtures `
  -TranslatorRoot build/stage3/spirv_to_dxil-dist `
  -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-shaders/gpu-tools
```

## Limitações

Este é um caminho GNM limitado e testável; **shaders gerais de jogos ainda não estão completos**. Faltam fetch/memória de shaders, recursos e texturas, interpolação fora do contrato acima e outras famílias GCN, loops e EXEC/VCC, tiling completo, depth, múltiplos render targets, draws indexados/indiretos, compute, CCB/CE/DE, eventos/labels, apresentação video-out e integração à biblioteca normal. Não há comprovação de boot comercial, gameplay ou execução Xbox/AppContainer. Nada foi instalado.

Próximo incremento: leitura de buffers por shaders com descritores PS4 e limites de memória explícitos, com teste guest que altere o buffer entre submissões e comparação pixel a pixel.
