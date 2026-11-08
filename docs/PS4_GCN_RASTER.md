# PS4 GCN e rasterização — contrato do incremento

Este incremento acrescenta shaders GCN e draw ao diagnóstico gráfico da etapa 4. O alvo é um triângulo gerado por vertex ID, sem fetch shader, texturas ou recursos de jogos. O suporte comercial e a submissão GNM pelo runtime continuam separados.

## Frontend e IR

`GcnDecoder.*` é um decoder próprio, bounded e transacional. Ele lê palavras GCN diretamente; não chama o decoder RDNA2. Somente instruções cuja representação semântica cabe no subconjunto abaixo são normalizadas em `Decoder::Instruction` para a tradução/IR existentes:

| Família | Instruções aceitas |
|---|---|
| VOP1 | `v_mov_b32`, `v_cvt_f32_u32` |
| VOP2 | `v_add_f32`, `v_sub_f32`, `v_mul_f32`, `v_lshlrev_b32`, `v_and_b32` |
| EXP | Um export completo, não comprimido, `done`: POS0 em VS ou MRT0 com `vm` em PS |
| SOPP | `s_endpgm`, obrigatoriamente depois do export e sem código posterior |

São aceitos VGPRs definidos, constantes inline inteiras/float do subconjunto e literal de 32 bits com comprimento verificado. Leituras indefinidas, SGPRs, special operands, branches/calls, memória, VOP3, SDWA/DPP, interpolação e exports adicionais/comprimidos são recusados com PC e motivo. Limite: 4096 palavras. Falhas não alteram o output.

`GcnCompiler.*` valida primeiro, seleciona wave64 e chama `TranslateProgram` e `CompileProgram` existentes para produzir SPIR-V. O ponto novo na entrada da IR é `gcn_vertex_inputs`: VS GCN recebe vertex ID em `v0`; a ABI NGG/RDNA2 PS5 continua sendo o padrão. Nenhuma conversão cega de registradores PS4 para Prospero foi introduzida. O wrapper não usa o cache de código-fonte RDNA2.

Referências locais conferidas: `shadPS4-main/src/shader_recompiler/frontend/opcodes.h`, `decode.cpp` e `translate/translate.cpp`, comparadas com `VectorAluOps.cpp`, `ExportOps.cpp` e `Translate.cpp` do Kyty. As fixtures e a implementação são próprias. Essas referências confirmam encodings, campos, operandos e entrada VS; a equivalência exercitada pelo diagnóstico não prova todos os casos numéricos de FP, NaN, denormals ou modos de arredondamento.

## Draw e backends

`Ps4::RasterState` fornece um contrato explícito e independente dos registradores PS5: triangle list, três vertices, first vertex/instance zero, uma instância, viewport cobrindo o alvo, scissor, RGBA8 UNorm, um sample, depth/stencil e culling desativados. Dimensões até 1024×1024; clear finito em [0,1]. Há máscara RGBA e blend RGB `src-alpha / 1-src-alpha`, com alpha `one / 1-src-alpha`.

- Vulkan: módulos SPIR-V produzidos pelo frontend GCN, pipeline gráfico, render pass com clear, `vkCmdDraw`, transição/dependência para cópia e readback após fence.
- D3D12: os mesmos shaders passam pelo Mesa-UWP `spirv_to_dxil_linked` e pelo validador DXIL. O pipeline usa RTV, `DrawInstanced`, scissor e blend/máscara correspondentes. A inversão Y condicional mantém a convenção de viewport usada pelo Vulkan. Root constants preservam o contrato existente de 12 dwords; o prefixo de runtime tem 36 bytes.
- Os adaptadores usam recursos privados e recebem device/queue existentes. Não espelham nem alteram o framebuffer de um jogo. Recursos ficam vivos até conclusão da GPU ou device loss. O chamador serializa a queue Vulkan.
- O diagnóstico D3D12 existente (`RunShaderSelfTest`) inclui a cadeia GCN → IR → SPIR-V → `DxilCompiler::CompileLinked` → draw/readback. Isso inclui o caminho compilado no aplicativo UWP; a execução em Xbox exige teste no dispositivo.

## Verificação e reprodução

`Ps4GcnShaderTests.cpp` cobre recusa de bytecode truncado, fontes inválidas/indefinidas, estágios errados, famílias/controles não suportados e modificação transacional do output. Emite quatro fixtures SPIR-V, validadas com SPIRV-Tools: VS fullscreen, VS de triângulo parcial, PS sólido e PS com alpha 0,5. Os binários GCN de entrada também são gravados para auditoria.

`Ps4RasterTests.cpp` confere os 2048 pixels de cada cena contra uma referência geométrica independente, com tolerância de um nível UNorm: triângulo parcial, triângulo fullscreen recortado por scissor, e triângulo parcial com blend e máscara R/B. Há pixels cobertos e descobertos em todos os casos; um simples clear/upload não satisfaz a referência. Estado inválido é recusado sem alterar a imagem anterior.

Com um checkout desktop já configurado e dependências existentes:

```powershell
cmake --build build/stage1-desktop --target ps4_gcn_shader_tests ps4_raster_vulkan_tests
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-gcn/fixtures
build/stage1-desktop/ps4_raster_vulkan_tests.exe build/stage4-gcn/fixtures
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-gcn/fixtures `
  -TranslatorRoot build/stage3/spirv_to_dxil-dist `
  -VulkanRoot C:/VulkanSDK/1.4.357.0 `
  -OutputDir build/stage4-gcn/gpu-tools
```

O script usa somente Visual Studio/clang-cl, SDKs e a distribuição Mesa já existentes. Compila o utilitário de DXIL e os probes desktop, valida/assina os shaders e executa D3D12 hardware/WARP e Vulkan. Não usa glslang nem shaders GLSL/HLSL substitutos. Não instala, registra ou implanta o aplicativo. Os arquivos `.ppm` resultam do readback da GPU.

## Limites

Não implementa shaders arbitrários, fetch/vertex buffers, interpoladores, texturas/descriptors, loops, branches, compute, múltiplos MRTs, depth/stencil, MSAA, tiling ou registradores GCN. Também não implementa `sceGnmSubmitCommandBuffers`, video-out PS4 ou boot comercial. O teste seleciona explicitamente o estado do draw; não é ainda um decoder de todo o estado PM4 PS4.

O caminho Xbox continua D3D12. O diagnóstico visa SM 6.4 e operações de 32 bits e não introduz dependência Vulkan em runtime UWP. O probe desktop Vulkan pede API 1.2 para os float controls declarados pelo emissor compartilhado; não altera os requisitos do renderizador principal. Build, execução desktop, AppContainer e Xbox devem ser reportados separadamente.

## Resultado observado — 2026-11-07

- `build/stage4-gcn/gcn-shaders.log`: decoder e quatro módulos SPIR-V aprovados no validador SPIRV-Tools.
- `build/stage4-gcn/gpu-final.log`: três pipelines ligados VS/PS, seis objetos DXIL validados/assinados com o Mesa-UWP já compilado; nenhum shader GLSL/HLSL substituto. SM 6.4.
- Mesmo log: nove cenas aprovadas (três em D3D12 Radeon RX 6600M, três em WARP e três em Vulkan Radeon RX 6600M). Cada cena confere todos os 2048 pixels; cobertura esperada de 576 pixels para o triângulo parcial e 1248 para o fullscreen recortado. Camada D3D12 habilitada e zero erros; Vulkan com `VK_LAYER_KHRONOS_validation`, zero erros.
- As três imagens são idênticas byte a byte entre os três caminhos; diferença máxima zero. `build/stage4-gcn/readback-comparison.json` contém hashes RGB e comparações. PNGs em `build/stage4-gcn/fixtures/{d3d12,vulkan,warp}-{0,1,2}.png`.
- `build/stage4-gcn/ctest-final.log`: 17/17 aprovados, incluindo `ps4_gcn_shader`, `ps4_gcn_raster_vulkan`, `shader_cfg`, as três verificações de transferências e regressões de recursos/CPU/HLE/plataforma. Dez atribuições de `vk::Extent3D` nos testes PS5 foram tipadas explicitamente para remover a ambiguidade existente com os headers Vulkan atuais.
- O núcleo desktop/Vulkan foi recompilado (`build-core*.log`). Os executáveis de teste de shader foram ligados reutilizando esses mesmos objetos de produção e comandos gerados pelo CMake, com debug removido para reduzir os executáveis de teste; `reuse-core-tests.py`, `reuse-cfg.py` e logs estão em `build/stage4-gcn`. Não é execução de gameplay PS5.
- `build/stage4-gcn/build-uwp-final.log`: build completo UWP/D3D12 aprovado com frontend GCN, raster probe e integração ao diagnóstico existente. `uwp-import-audit.log`: imports nomeados do layout aprovados contra `WindowsApp.lib`; imports por ordinal e comportamento no dispositivo não são cobertos.

Nada foi instalado, registrado ou implantado. O aplicativo UWP e o Xbox não foram executados. A meta de shaders/draw foi atendida no **subconjunto diagnóstico descrito**, com shaders GCN reais; a integração GNM e shaders de jogos continuam pendentes.
