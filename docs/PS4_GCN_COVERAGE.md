# GCN PS4 — interpolação e cobertura por família

Atualização 2026-11-08: [IMAGE_LOAD RGBA8 e recursos VS/PS simultâneos](PS4_TEXTURES.md) validados: até dois buffers e uma textura por estágio, ELF com seis payloads, 25/25 testes, Vulkan/D3D12/WARP e build UWP aprovados. Samplers/filtragem, demais formatos/tiling e fase 4 geral permanecem incompletos.


Atualização 2026-11-08: [MUBUF indexado, dois buffers e vertex fetch](PS4_MUBUF.md) implementados e validados no contrato limitado: 24/24 testes, ELF guest com índice/dados mutáveis, Vulkan/D3D12/WARP e build UWP aprovados. Texturas/MIMG, buffers simultâneos VS+PS e a fase 4 geral continuam incompletos.


Atualizado em 2026-11-08. **Incremento concluído e validado; suporte a todas as famílias GCN e fase 4 continuam INCOMPLETOS.** Esta matriz descreve o frontend PS4 real, não o catálogo maior da IR PS5 compartilhada.

## Incremento implementado

`GcnCompiler.*` recebe um mapa opcional de entradas PS para slots PARAM e seleção de interpolação linear central. O mapa tem exatamente a contagem declarada (até quatro entradas); cada localização deve estar no intervalo de slots exportados. Mapa vazio conserva identity. Falhas preservam a saída SPIR-V. A tradução usa os interpolantes nativos existentes; não introduz uma segunda implementação de interpolação.

`ps4Gnm.cpp` lê SPI_PS_INPUT_CNTL_0..3 como localizações smooth, em vez de exigir identity. SPI_PS_INPUT_ENA e ADDR devem coincidir: 2 para perspectiva central ou 32 para linear central. A contagem VS/PS ainda é compartilhada e limitada a quatro. Flags flat, default e demais controles continuam recusados. A validação do lote inteiro precede os efeitos do renderer.

P1/P2 F32 permanecem pares canônicos adjacentes, com o mesmo destino/atributo/canal e fontes v0/v1. A IR calcula o atributo completo em P2; P1 é eliminada. Por isso não são aceitos intermediários P1 observáveis, pares separados, branch entrando em P2, MOV de parâmetros, interpolação manual, sample/centroid ou F16. No modo linear, as coordenadas do prólogo não são consumidas por esses pares e desaparecem por DCE; o input nativo recebe NoPerspective. Nenhuma mudança foi feita na ABI ou no emissor compartilhado PS5.

`GcnDecoder.cpp` acrescenta sete VOP1 GCN: CVT_F32_I32 (5), FRACT_F32 (32), TRUNC_F32 (33), CEIL_F32 (34), FLOOR_F32 (36), RCP_F32 (42), SQRT_F32 (51). Fontes/definição de registradores e limites continuam sujeitos à validação existente. As operações são normalizadas para traduções já existentes na IR.

## Cobertura de todas as famílias do perfil e extensões a discriminar

“Parcial” significa somente o subconjunto explicitamente listado; nenhuma linha implica shaders gerais completos.

| Família | Estado do frontend PS4 | O que falta / dependência |
|---|---|---|
| SOP1 | Parcial: MOV_B32, NOT_B32 | Operações de 64 bits, EXEC/VCC, setpc/swappc, manipulação de wave e análise de fluxo correspondente |
| SOP2 | Parcial: add/sub 32, min/max 32, cselect, bitwise, shifts, mul | Carry/borrow, 64 bits, máscaras e semântica completa de SCC |
| SOPK | Parcial: MOVK_I32 | Comparações/imediatos restantes e seus efeitos em SCC/fluxo |
| SOPC | Parcial: comparações signed/unsigned 32 | Demais comparações, bit tests e pares 64 bits |
| SOPP | Parcial: NOP, END, branches forward SCC, WAITCNT restrito | Loops, branches VCC/EXEC, barreiras, mensagens e contadores/sincronização gerais |
| SMRD | Parcial: S_BUFFER_LOAD_DWORDX4 PS com um descriptor real | Demais larguras/offsets, S_LOAD indireto, múltiplos recursos, tabelas e outros estágios |
| VOP1 | Parcial: MOV, CVT_F32_U32 e as sete operações deste incremento | Conversões/bit scans restantes, lane ops, F16/F64 e modos numéricos completos |
| VOP2 | Parcial: ALU F32 e operações bitwise/shifts do decoder | CNDMASK, carry/borrow, min/max inteiros, MAC e dependências VCC/EXEC |
| VOPC | Não implementada | Comparações vetoriais, VCC e variantes CMPX com atualização de EXEC; convergência por lane |
| VOP3 | Parcial: add/sub/subrev/mul/min/max F32, MAD/FMA e modificadores restritos | Restantes opcodes, destinos especiais, VOP3B, 64 bits e semântica de operandos/modificadores por opcode |
| VINTRP | Parcial: P1/P2 F32, perspectiva/linear central, mapa smooth | MOV, flat/default/custom, sample/centroid, F16 e uso real de coeficientes/intermediários |
| EXP | Parcial: POS0/PARAM0..3 VS e MRT0 PS completos | Máscaras parciais/compressão, múltiplos MRTs, depth/sample mask e estágios adicionais |
| DS | Não implementada | LDS por workgroup, endereçamento, barreiras, atomics e suporte a compute/estágios pertinentes |
| MUBUF | Parcial: DWORD/DWORDX4 IDXEN, dois buffers, VS/PS e vertex fetch inline | OFFEN/SOFFSET dinâmicos, demais larguras/controles, stores/atomics, formatted fetch e coerência; contrato em [PS4_MUBUF.md](PS4_MUBUF.md) |
| MTBUF | Não implementada | Recursos MUBUF + conversão de formatos numéricos/typed fetch e ligação ao vertex fetch |
| MIMG | Parcial: IMAGE_LOAD RGBA8 linear 2D, uma imagem por VS/PS | Samplers, sample/LOD/gather, demais formatos/tiling, mips/arrays, writes/atomics e coerência geral; [contrato](PS4_TEXTURES.md) |
| FLAT / SMEM de gerações posteriores | Fora do contrato de encoding atual | Discriminar geração Liverpool/Neo antes de introduzir encodings e modos de endereço diferentes de SMRD |
| VOP3P, SDWA/DPP e extensões | Fora do contrato atual | Separar a geração que realmente oferece cada extensão; não interpretar encodings RDNA2 como PS4. Precisam semântica packed/lanes e testes próprios |

VOP3P não deve ser tratado como requisito automático de Liverpool só por aparecer no decoder de referência. A lista de formatos da referência cobre mais gerações que o contrato atual do Kyty. SDWA/DPP são extensões de instruções, não uma autorização genérica para todas as gerações PS4.

## Partes e dependências para continuar

1. **VCC/EXEC e VOPC → CNDMASK e branches por máscara.** Definição/uso dos pares de máscaras, atualização CMPX, wave64, testes de lanes e divergência; só então ampliar os opcodes dependentes. O controle atual por SCC não substitui esse trabalho.
2. **Recursos de buffer → MUBUF → MTBUF/fetch.** Reutilizar o descriptor e o binding raw comprovados, começar por um load read-only com endereço/index validados e payload que muda entre submissões. Expandir depois para múltiplos descriptors, VS, formatos, stores/atomics e sincronização.
3. **ABI de interpolação → flat/default e MOV.** Distinguir PARAM exportado de entrada PS, contagens independentes e coeficientes por vértice. Não usar a aproximação fallback de MOV em smooth como equivalência completa de hardware. Sample/centroid dependem de MSAA/estado e capacidades dos dois backends.
4. **Imagens/samplers e tiling → MIMG.** Não basta mapear o opcode para a IR: a imagem guest precisa existir no host, ter layout/formato corretos e lifetime protegido por conclusão da GPU.
5. **Dispatch/workgroups/LDS → DS/barreiras/atomics.** Precisa caminho compute GNM/runtime e validação por readback, além de shader decoding.
6. **ISA/CFG ampliados → loops/calls/64 bits e extensões por geração.** Manter erros explícitos para controles não implementados e regressões PS5 por alteração compartilhada.

O incremento MUBUF indexado foi validado em [PS4_MUBUF.md](PS4_MUBUF.md). A textura RGBA8 com IMAGE_LOAD e recursos simultâneos VS/PS foi validada em [PS4_TEXTURES.md](PS4_TEXTURES.md). Próximo incremento recomendado: sampler nearest/clamp e IMAGE_SAMPLE_L com LOD0, antes de ampliar formatos/tiling. VOPC/EXEC deve seguir como incremento separado antes de divergência geral. A ordem pode mudar se uma fixture real revelar outro bloqueio mais próximo.

## Evidências desta execução

Diretório: `build/stage4-interpolation/`.

- `build-final.log`: GCC compila os três arquivos de produção alterados e quatro entradas de testes; ligação reutiliza os demais objetos do core já compilados, com comandos do compdb local. Não é alegado um rebuild limpo do aplicativo desktop inteiro.
- `shaders-final.log`: 16 fixtures SPIR-V emitidas, além de checks de interface/negativos, validadas com SPIRV-Tools. `remap.spvasm` confirma Location 3 para a entrada zero; `linear.spvasm` confirma NoPerspective.
- `unary.spvasm`: loads de buffer e ConvertSToF, Sqrt, Trunc, FDiv, Ceil, Floor e Fract presentes. A fixture lê SGPRs vindos de SMRD em buffer GPU, impedindo que toda a ALU seja resolvida como constantes de compilação. Inteiro -3 e floats negativos exercitam conversão signed e arredondamento.
- `gpu-buffer-alu-final.log`: 26 objetos DXIL ligados/validados em SM 6.4; 15 variantes de rasterização e cenário misto de transferências/draws em cada backend D3D12 Radeon RX 6600M, WARP e Vulkan. Cada variante confere 2048 pixels RGBA contra referência CPU, tolerância de um nível UNorm. Zero erros D3D12/Vulkan.
- Novas cenas: variante 12 mapeia entrada PS zero para PARAM3 com canais distintos; 13 executa as sete VOP1 com valores de buffer; 14 usa W diferente entre vértices e interpolação linear central. `readback-comparison.log`: D3D12 hardware/Vulkan idênticos nas três; WARP difere em até um nível RGB nos gradientes, e é idêntico no caso ALU.
- `ctest-final.log`: 23/23 aprovados, incluindo runtime ELF GNM, interpolação perspectiva anterior, buffers/transferências, PS5 shader_cfg/scalar_provenance/resource_tracking/materialization e plataformas/loader/HLE. O novo mapa e seleção linear passam por estado GNM com renderer de gravação no teste unitário; os novos modos são executados em GPU pelos probes. Não se alega ELF guest exercitando os novos modos.
- `build-vulkan-final.log`: probe desktop Vulkan recompilado e ligado. `build-uwp-final.log`: build completo incremental UWP/D3D12 e layout aprovados. `audit-uwp.log`: imports WindowsApp.lib PASS. Nenhuma instalação, deploy ou execução AppContainer/Xbox.
- `gpu-linear-reference-error.log`: primeira validação linear encontrou fator perspectiva indevido na referência CPU; corrigido e repetido. Falha preservada, resultado final no log acima.

## Reprodução com dependências existentes

```powershell
C:/msys64/ucrt64/bin/python.exe build/stage4-interpolation/build-tests.py ps4_gcn_shader_tests ps4_gnm_tests shader_cfg_tests ps4_runtime_tests
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-interpolation/fixtures
cmake --build build/stage1-desktop --target ps4_raster_vulkan_tests -j4
ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-interpolation/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-interpolation/gpu-tools
src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp
build/stage3/audit-uwp-imports.ps1
```

Os helpers em build são artefatos locais e usam o compdb já existente. Para uma árvore nova, usar os targets CMake normais `ps4_gcn_shader_tests`, `ps4_gnm_tests`, `ps4_runtime_tests`, `shader_cfg_tests`, além do probe Vulkan. Preparar PATH MSYS/Qt para executáveis GCC e KYTY_LAZY_MEMORY=1 conforme o ambiente dos testes. Nada deve ser instalado para reproduzir no ambiente atual.

## Referências e limites

Consultados `shadPS4-main/src/shader_recompiler/frontend/opcodes.h`, `decode.cpp` e `src/video_core/amdgpu/regs_shader.h`: valores GCN VOP1, formatos e bits persp_center/linear_center. Conferidos `sharpemu-main/src/SharpEmu.GnmMode/Shaders/GcnShaderDecoder.cs` e a separação GFX6/7 → IR normalizada. São referências de leitura; a implementação e as fixtures são próprias, usando traduções já existentes no Kyty. Nenhum código das referências foi copiado.

Não certifica casos especiais de FP (NaN/Inf/denormal, exceções, todos os arredondamentos), todas as instruções de cada família, shaders comerciais, apresentação/video-out ou Xbox. O lançamento PS4 normal permanece recusado. A missão global segue parcial.
