# PS4 — descriptor de buffer e SMRD com recurso GPU

Atualização 2026-11-08: [IMAGE_LOAD RGBA8 e recursos VS/PS simultâneos](PS4_TEXTURES.md) validados: até dois buffers e uma textura por estágio, ELF com seis payloads, 25/25 testes, Vulkan/D3D12/WARP e build UWP aprovados. Samplers/filtragem, demais formatos/tiling e fase 4 geral permanecem incompletos.


Atualização 2026-11-08: [MUBUF indexado, dois buffers e vertex fetch](PS4_MUBUF.md) implementados e validados no contrato limitado: 24/24 testes, ELF guest com índice/dados mutáveis, Vulkan/D3D12/WARP e build UWP aprovados. Texturas/MIMG, buffers simultâneos VS+PS e a fase 4 geral continuam incompletos.


Data: 2026-11-08. Estado: **CONCLUÍDO no contrato descrito**. Incremento inicial de P1/P2, não suporte completo a buffers ou shaders gerais.

## Caminho implementado

O decoder GCN reconhece s_buffer_load_dwordx4, normaliza a instrução para o S_BUFFER_LOAD_DWORDX4 da tradução existente e preserva offsets GCN em DWORDs convertendo-os para bytes. A tradução existente Translator::S_LOAD gera quatro ReadConstBuffer; ResourceTracking, especialização, CollectShaderInfo e o emissor SPIR-V existentes produzem acessos reais ao recurso. Os quatro resultados entram em SGPRs e podem alimentar ALU/exports normalmente.

A CPU valida o descriptor e captura os bytes guest sob a lease de memória do perfil. O conteúdo NÃO é convertido em constantes do shader. Cada draw recebe o snapshot do recurso; o shader lê um buffer ligado à GPU. O ELF de teste muda o payload entre submissões, mantendo bytecode e descriptor, e precisa obter outra imagem.

## Contrato inicial

- Pixel shader somente, um descriptor direto nos user SGPRs s0..s3; quatro user DWORDs no RSRC2. O VS mantém o caminho existente.
- Descriptor GCN de quatro DWORDs: base de 40 bits, stride zero, sem swizzle de endereços/flags extras, raw com seleção de canais identity (word3=0xFAC); num_records tratado como tamanho em bytes para stride zero.
- Endereço não nulo, alinhado a 16 bytes; tamanho entre 16 e 65536 bytes, múltiplo de quatro e sem cruzar o espaço de 40 bits. Outros modos são recusados, não reinterpretados como Prospero.
- Offset imediato de oito bits em DWORDs; os 16 bytes do load precisam caber no descriptor. Offset por SGPR, literais extendidos, outros SMRD e descriptors indiretos ficam pendentes.
- Destino s4..s103 em grupo de quatro, dentro da alocação SGPR declarada; definição por caminho inclui as quatro palavras do descriptor e os quatro resultados. Mutação de s0..s3 pelo shader é recusada neste contrato.
- s_waitcnt lgkmcnt(0), com vmcnt/expcnt máximos (immediate 0x7F), é aceito. A IR ordena os loads e seus consumidores; não modela latência/caches GCN por ciclo.
- Buffer read-only, capturado por draw. Alias com qualquer alvo de draw no mesmo lote é recusado: o perfil ainda não resolve leitura de resultado GPU de um draw anterior como buffer de shader.

## ABI de recursos e hosts

A especialização raw usa a estrutura de buffers existente. O wrapper GCN verifica que o resultado exige exatamente um buffer e dois DWORDs de shader data: prefixo/offset de binding zero e range em bytes. O bloco SPIR-V continua com 128 bytes, conforme PushData existente.

O wrapper normaliza somente as decorações do binding GCN para set0/binding58 e NonWritable. Isso unifica os módulos gerados em builds Vulkan e D3D12 sem alterar a política de bindings do PS5. Não há transformação de instruções RDNA em instruções GCN por esse passo.

- Vulkan: SSBO host-visible/coherent de leitura, descriptor set com binding58, push-constant range de 128 bytes; os dois DWORDs usados são atualizados antes do draw. Buffer e descritores permanecem vivos até fence.
- D3D12: upload para buffer default, transição COPY_DEST→PIXEL_SHADER_RESOURCE, SRV raw t58/space0 em heap shader-visible. Root constants b0/space1000 transportam os dois DWORDs usados; as constantes de runtime Mesa em space1001 continuam preservadas. Recursos/upload/heap permanecem vivos até fence/readback.
- DxilCompiler de produção e Mesa-UWP continuam responsáveis por tradução/ligação. Não há dependência Vulkan no executável UWP.

## Arquivos

| Arquivo | Responsabilidade |
|---|---|
| GcnDecoder.h/.cpp | SMRD, wait, grupos de SGPRs e fixture GCN própria |
| GcnCompiler.h/.cpp | Parser de descriptor, validação de offset/imutabilidade, especialização e ABI do binding |
| src/libs/ps4Gnm.cpp | Snapshot dos dados, checagem do grupo SGPR e recusa de alias com alvos |
| ps4Raster.h | Payload read-only opcional e limites no contrato de host |
| d3d12/ps4RasterProbe.cpp | Recurso default, SRV/upload/barreira e constantes |
| renderer/ps4TransferProbe.cpp | SSBO, pool/set de descriptors, push constants e lifetime |
| Ps4GcnShaderTests / Ps4GcnDxilTests / Ps4RasterTests | Validadores, modos inválidos e comparação pixel a pixel |
| Ps4GnmTestFixture.h / Ps4GnmTests.cpp / CMakeLists.txt | ELF AMD64 e testes GNM de dados mutáveis, bindings e rejeições |

Os nomes abreviados acima estão nos diretórios existentes de shader/recompiler, graphics/host_gpu ou tests; não foi criado um backend paralelo. Código/fixtures próprios.

## Verificação final

Diretório: build/stage4-buffers/.

- shaders.log: primeiro shader SMRD passou no validador SPIR-V.
- gpu.log: primeira execução produziu pixels corretos, mas Vulkan apontou layout de push constants de 8 bytes para bloco declarado de 128. Corrigido; não contar essa execução como aprovação final.
- gpu-final.log: casos novos passaram com dois conteúdos do mesmo buffer em D3D12, WARP e Vulkan; zero erros debug/validation. Vinte objetos DXIL passaram no validador, incluindo o PS com recurso.
- build-uwp.log: build UWP completo aprovado. Prova de compilação/layout, não execução no Xbox.
- shaders-final.log: 13 fixtures SPIR-V, verificações de interface anteriores e negativos de descriptors/SMRD aprovados; gcn-buffer.frag.gcn contém bytecode próprio.
- guest.log e ctest-details.log: ELF AMD64 → GNM → SMRD → buffer GPU; alteração de payload entre submissões mantendo descriptor/shader, status 0x600d474e, dois draws, uma rejeição e todos os 2048 pixels finais conferidos.
- unit.log: snapshots diferentes com SPIR-V idêntico; erros de endereço/modo/tamanho/offset e alias recusados antes do renderer e sem publicar dados.
- ctest-final.log: **23/23 aprovados**, incluindo ps4_gnm_buffer e regressões selecionadas PS5 de CFG/proveniência/recursos.
- build-desktop.log/relink.log: núcleo desktop/Vulkan e testes aprovados, com os mesmos objetos finais e comandos gerados pelo CMake; debug removido dos executáveis de teste para reduzir tamanho.
- audit-uwp.log: imports UWP contra WindowsApp.lib aprovados. Build UWP passou; execução AppContainer/Xbox não ocorreu.
- readback-comparison.log: imagens dos dois conteúdos de buffer idênticas nos três renderizadores.

A evidência de guest completa usa o backend Vulkan desktop. D3D12 foi exercitado nos probes do mesmo frontend/IR/Mesa, com recurso default/SRV real; o perfil GNM foi compilado em UWP/D3D12, sem afirmar execução do ELF guest nesse ambiente.

## Limites e próximo passo

Faltam múltiplos buffers, recursos em VS/compute, descriptors/tabelas indiretas, offsets dinâmicos, stride/formatos, loads de outras larguras, stores/atomics, texturas, coerência de aliases e apresentação. O perfil continua síncrono, com snapshots/upload/readback por draw. Não é cache persistente nem protocolo geral de coerência diante de escritas concorrentes guest.

Próximo incremento: ampliar seleção de descriptors/offsets e recursos de vertex fetch (MUBUF), mantendo validação de limites e bindings reais; só então avançar os modos de buffer requeridos pelas fixtures/títulos.

Referências somente leitura: shadPS4 frontend/decode.cpp (campos SMRD), frontend/translate/scalar_memory.cpp (offset em DWORDs), video_core/amdgpu/resource.h (descriptor de 40 bits); SharpEmu GcnBufferDescriptor.cs (separação GCN/Gen5). Nenhuma função das referências foi copiada. Não houve instalação/deploy ou comprovação de execução Xbox/AppContainer.

## Reprodução

```powershell
cmake --build build/stage1-desktop --target ps4_gnm_tests ps4_gcn_shader_tests ps4_raster_vulkan_tests
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-buffers/fixtures
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-buffers/guest --buffer
ctest --test-dir build/stage1-desktop --output-on-failure -R ps4_gnm_buffer
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-buffers/fixtures `
  -TranslatorRoot build/stage3/spirv_to_dxil-dist `
  -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-buffers/gpu-tools
```

Nada foi instalado ou implantado. A fase 4 e a missão de compatibilidade comercial permanecem abertas.
