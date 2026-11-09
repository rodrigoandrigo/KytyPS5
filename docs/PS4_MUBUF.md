# PS4 — MUBUF indexado, dois buffers e vertex fetch

Continuação P3 (2026-11-08): [MTBUF, fetch externo e índices16/32](PS4_LUMEN_INTEGRATION.md) escritos e ligados ao GNM/adaptadores, **sem compilação ou testes**. O novo export sceGnmDrawIndex também permanece não validado. Resultados anteriores abaixo são históricos.

Atualização 2026-11-08: [IMAGE_LOAD RGBA8 e recursos VS/PS simultâneos](PS4_TEXTURES.md) validados: até dois buffers e uma textura por estágio, ELF com seis payloads, 25/25 testes, Vulkan/D3D12/WARP e build UWP aprovados. Samplers/filtragem, demais formatos/tiling e fase 4 geral permanecem incompletos.


Data: 2026-11-08. Estado: **CONCLUÍDO no contrato descrito**. Texturas e a fase 4 geral continuam incompletas.

## Contrato implementado

O frontend próprio aceita MUBUF BUFFER_LOAD_DWORD e BUFFER_LOAD_DWORDX4 em VS ou PS: IDXEN=1, OFFEN=0, SOFFSET inline zero, offset imediato alinhado a quatro bytes, sem GLC/SLC/TFE/LDS/ADDR64. O offset e todos os DWORDs devem caber no record. Outros controles/opcodes são recusados antes de produzir SPIR-V ou renderizar. Não há stores/atomics, formatted/typed fetch ou chamada externa de fetch shader.

Descriptors diretos em s0..3 e s4..7, até dois recursos vivos no estágio que usa memória. O wrapper retorna a lista na ordem dos recursos da IR, com endereço, tamanho em bytes e stride. Há validação das quatro palavras imutáveis de cada descriptor, dos grupos de destino e das alocações RSRC1. SGPRs não declarados e VGPRs indefinidos continuam sendo erros. O buffer SMRD original conserva stride zero e offsets escalares anteriores.

O descriptor MUBUF usa endereço de 40 bits alinhado a 16, stride não zero e múltiplo de quatro, num_records*stride entre 16 e 65536 bytes e word3=0xFAC (raw/identity). Multiplicação e limite do espaço guest são conferidos em 64 bits antes de capturar memória. Isso não implementa todos os descriptors GCN nem os reinterpreta como RDNA2.

A tradução existente gera LoadBufferU32/U32x4 e especialização de stride. O wrapper PS4 acrescenta UMin32(index,num_records) antes do cálculo do endereço: o índice inválido se torna um record sentinela imediatamente além da faixa. O emissor existente retorna zero nos acessos fora da faixa, incluindo todos os componentes X4. Assim UINT32_MAX e índices cujo produto em 32 bits voltaria ao começo não leem um record válido por overflow. O payload não vira constante de compilação.

O GNM captura ambos os recursos sob a lease guest, recusa cada alias com qualquer draw target do lote, preserva dados até a conclusão e publica o resultado somente após validar/executar o lote. Buffers simultâneos em VS e PS no mesmo draw são explicitamente recusados: os dois estágios ainda compartilham o bloco inicial de metadata do diagnóstico. Duas entradas de descriptor em um estágio estão disponíveis; isso não é um sistema ilimitado de bindings.

## Hosts e ABI

RasterState conserva o nome histórico pixel_buffer para o primeiro recurso (agora VS ou PS) e recebe pixel_buffer2. A lista tem um ou dois elementos, nunca somente o segundo; cada snapshot tem 16..65536 bytes e alinhamento de tamanho DWORD.

SPIR-V GCN: set0/binding58, array de buffers NonWritable, sem alterar a política PS5. Metadata para dois recursos: três DWORDs — offsets byte compactados (todos zero neste perfil), tamanho do recurso zero e tamanho do recurso um. O bloco SPIR-V/pipeline Vulkan continua 128 bytes. Para um recurso preserva os dois DWORDs anteriores.

D3D12: dois recursos default/upload e SRVs raw t58/t59 space0, tabela visível em VS/PS e constantes b0/space1000; barreira para PIXEL_SHADER_RESOURCE|NON_PIXEL_SHADER_RESOURCE. Vulkan: duas entradas SSBO no binding58, buffers host-visible/coherent, metadata via push constants e visibilidade VS/PS. Alocações e descritores são retidos até fence, inclusive nos caminhos de erro já existentes.

## Fixtures próprias

- IndexedBufferPixelFixture: SMRD lê o índice do primeiro buffer; MUBUF usa esse índice para escolher um RGBA32F raw no segundo. Variante X4 e variante com quatro loads DWORD e offsets 0/4/8/12. Nenhum dado de conteúdo é fornecido ao compilador.
- FetchVertexFixture: VertexIndex v0 indexa posições float4 do primeiro buffer e cores float4 do segundo; exporta POS0 e PARAM0. É vertex fetch inline real na GPU, sem input-layout/HLSL substituto.
- Probes: índices 1/2, limite 4, 0x10000000 e UINT32_MAX; os dois últimos expõem possível wrap de stride*índice. Posições/cores alteradas preservando bytecode mudam a geometria e os pixels.
- GNM unitário: snapshots independentes, SPIR-V estável, inválidos no segundo descriptor, endereço inacessível, alias e grupo RSRC1 insuficiente sem efeitos. Modo PS e modo VS.
- ELF AMD64 --indexed: imports GNM reais, dois descriptors, índice/payload alterados entre submissões, lote inválido recusado e todos os pixels finais conferidos (aprovado: status 0x600d474e, duas submissões, dois draws e um lote inválido recusado).

## Texturas: parte ainda ausente

MIMG continua recusado. Não há descriptor PS4 de imagem/sampler, upload de textura guest, detiling, sample/LOD/gather, image stores ou integração de seus bindings no diagnóstico. A imagem RGBA8 de destino do rasterizador não equivale a uma textura que o shader lê.

Sequência testável: (1) descriptor 2D linear RGBA8 read-only e snapshot de tamanho/pitch verificados; (2) criação/upload/layout e lifetime de imagem nos dois hosts, com binding separado dos buffers; (3) IMAGE_LOAD com coordenadas inteiras e comparação completa de texels; (4) sampler nearest/clamp e IMAGE_SAMPLE_L; (5) formatos/tiling/mips/filtragem gerais. Manter fixture legal com padrão que revele orientação, pitch, canais e bordas, além de updates entre submissões. Em paralelo, ampliar metadata independente VS/PS para usar recursos nos dois estágios simultaneamente.

## Referências e arquivos

Referências somente leitura: shadPS4 frontend/decode.cpp (decodeInstructionMUBUF), opcodes.h e resource.h; SharpEmu GcnBufferDescriptor.cs e separação GCN→IR. Implementação e fixtures próprias, sem cópia das referências.

Código: GcnDecoder.*, GcnCompiler.*, ps4Gnm.cpp, ps4Raster.h, d3d12/ps4RasterProbe.cpp, renderer/ps4TransferProbe.cpp. Testes: Ps4GcnShaderTests.cpp, Ps4GcnDxilTests.cpp, Ps4RasterTests.cpp, Ps4GnmTestFixture.h, Ps4GnmTests.cpp, run-ps4-gcn-gpu.ps1, CMakeLists.txt. Logs locais em build/stage4-mubuf/; resultados finais abaixo.

## Validação final e reprodução

- `shaders-final.log`: 19 fixtures SPIR-V validadas, incluindo três novas (VS fetch, PS X4 e PS DWORD), além de negativos. Sem conteúdo proprietário.
- `indexed.spvasm` / `fetch.spvasm`: array de dois recursos, loads de dados e UMin antes do produto stride*índice. `gpu-final.log`: 32 objetos DXIL ligados e validados em SM 6.4, 23 variantes por backend D3D12 Radeon RX 6600M, WARP e Vulkan, mais cenário misto de transferências/draws nos três; todos 2048 pixels RGBA por variante e zero erros debug/validation.
- Variantes 15/16 selecionam records diferentes; 17/18/19 verificam limite/overflow; 20/21 mantêm o shader e mudam posições/cores de dois buffers, com 576/256 pixels cobertos; 22 usa quatro loads DWORD com offsets distintos. Negativos de host cobrem segundo recurso sem primeiro e segundo recurso curto sem alterar a saída.
- `readback-comparison.log`: nos oito casos novos, D3D12 hardware/Vulkan idênticos. WARP difere em até um nível RGB no gradiente de fetch inicial, dentro da tolerância CPU; demais casos novos idênticos.
- `unit-final.log`: snapshots e módulos estáveis com dados mutáveis, segundo descriptor/endereço/stride/tamanho/formato inválido, alias de alvo, grupos RSRC1 e recursos simultâneos nos dois estágios recusados sem efeitos. Os modos VS e PS foram exercitados.
- `guest.log`: ELF AMD64 importa os builders/submits GNM, acessa dois buffers GPU, muda índice e conteúdo entre submissões e confere a imagem final inteira; status 0x600d474e, 2 submissões, 2 draws, 1 rejeição esperada. Novo fetch VS foi executado em GPU pelos probes e pelo estado GNM no teste unitário; o ELF novo exercita o MUBUF PS.
- `ctest-final.log` e `ctest-details-final.log`: **24/24 aprovados**, incluindo `ps4_gnm_indexed`, testes PS4 anteriores e regressões PS5 shader_cfg/scalar_provenance/resource_tracking/materialization. Gameplay PS5 não foi executado.
- `build-desktop.log`: build CMake do core desktop completo e probe Vulkan aprovado. A repetição em `build-desktop-final.log` encontrou entrada Vulkan omitida durante limpeza da macro de formatação; **esse log não é aprovação final**. Restaurada GetImageMemoryRequirements; `build-final-hosts.log` comprova recompilação final do probe nos dois targets e do teste raster, e `link-final.log` religa quatro executáveis com o core final. A metadata Ninja também reportou “premature end of file; recovering” e provocou recompilação ampla; nenhum código foi aceito com base nessa falha. Testes/guest/probes posteriores passaram. Não foi compilada a interface desktop completa.
- `build-uwp-final.log`: build incremental completo UWP/D3D12 e layout aprovados; `audit-uwp.log`: imports WindowsApp.lib PASS. Nenhuma instalação, deploy, AppContainer ou execução Xbox.
- Erros anteriores corrigidos: a metadata de dois buffers ocupa três DWORDs, não quatro (`shaders.log`); declaração auto de dois tipos diferentes no teste (`relink.log`); entrada da macro Vulkan perdida durante reformatar (`build-desktop-final.log`). Os resultados aceitos são os logs finais acima.

Com dependências já existentes, PATH MSYS/Qt para os testes GCC e KYTY_LAZY_MEMORY=1:

```powershell
cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_gcn_shader_tests ps4_gnm_tests ps4_raster_vulkan_tests shader_cfg_tests -j4
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-mubuf/fixtures
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-mubuf/guest --indexed
ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-mubuf/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-mubuf/gpu-tools
src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp
build/stage3/audit-uwp-imports.ps1
```

Na execução, os demais testes foram ligados aos mesmos objetos do core já compilado com comandos CMake preservados em compdb (`link-only.py` local), evitando recompilar o núcleo por cada entrada de teste. A compilação direta final dos objetos Vulkan usou os mesmos comandos. Esses auxiliares em build são artefatos locais; os targets acima são a reprodução normal para outro checkout.

**Limites:** máximo de dois buffers vivos, um estágio com buffers por draw; descriptors diretos/imóveis, recursos read-only capturados por draw, sem leitura de draw target como buffer, sem cache/coerência geral. Sem texturas, sampler, tiling, MIMG, MTBUF, draw indexado/instancing, fetch externo ou compatibilidade comercial. Fase 4 e o objetivo abrangente de recursos/texturas continuam INCOMPLETOS.

**Próxima tarefa concreta:** textura 2D linear RGBA8 read-only com descriptor PS4, snapshot/pitch/bounds, recurso de imagem real e upload nos dois hosts, IMAGE_LOAD e fixture padrão com todos os texels verificados; depois sampler nearest/clamp + IMAGE_SAMPLE_L. Separadamente, reservar metadata independente VS/PS para recursos nos dois estágios simultaneamente.
