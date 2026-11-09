# PS4 — textura 2D RGBA8 e recursos simultâneos VS/PS

Implementação em andamento 2026-11-08: [checkpoint Lumen](PS4_LUMEN_INTEGRATION.md) registra adaptações de SMRD/ALU, sampler e diagnóstico GNM **ainda não compiladas ou testadas**. Integração P0..P15 INCOMPLETA; testes adiados até a integração solicitada estar completa. As aprovações anteriores abaixo não validam este novo código.

Auditoria documental 2026-11-08: [contribuições do Lumen para a fase 4](PS4_LUMEN_GPU_AUDIT.md) mapeia samplers/ISA, recursos/layouts, coerência/sincronização, VideoOut, testes e limites de portabilidade/licença. Nenhum código Lumen foi integrado ou executado; as capacidades PS4 do Kyty permanecem as validadas abaixo.


Data: 2026-11-08. **Incremento CONCLUÍDO e validado no contrato abaixo.** A fase 4 e a compatibilidade PS4 geral continuam parciais. Nada instalado ou deployado.

## Estado anterior e alteração

O frontend recusava MIMG e o GNM recusava buffers simultâneos nos dois estágios, pois compartilhavam binding58 e os primeiros DWORDs de metadata. Agora cada estágio pode usar **até dois buffers read-only e uma textura 2D read-only no mesmo draw**. A fixture principal liga quatro buffers e duas texturas reais, com conteúdo distinto em cada estágio.

O decoder GCN próprio reconhece IMAGE_LOAD (opcode MIMG 0), sem passar pelo decoder RDNA2. A tradução existente IMAGE_LOAD → ImageRead → OpImageFetch é reutilizada. Não foram alteradas a ABI, as passes nem a emissão compartilhadas PS5.

## Descriptor e instrução aceitos

O descriptor GCN T# tem oito DWORDs, direto e imutável nos user SGPRs s8..15. O estágio declara 16 user SGPRs; s0..3/s4..7 ficam disponíveis aos buffers diretos anteriores. O conteúdo da imagem não é fornecido ao compilador.

| Campo | Contrato |
|---|---|
| Endereço | word0 << 8, não nulo, limite do espaço guest de 40 bits; demais bits de endereço/mtype de word1 recusados |
| Formato | DATA_FORMAT=10 (8_8_8_8), NUM_FORMAT=0 (UNORM); word1 exato 10<<20 |
| Dimensões | width=(word2&0x3fff)+1, height=((word2>>14)&0x3fff)+1; 1..256 cada; bits altos reservados zero |
| Tipo/seleção | Color2D=9, RGBA identity, tiling_index=8 DisplayLinearAligned; word3 exato 0x90800FAC |
| Pitch | ((word4>>13)&0x3fff)+1 em texels, convertido para bytes; >=width*4, <=4096 e múltiplo de 256 bytes |
| Depth/array/mip | Uma camada, um mip, level0; outros campos de words4..7 zero |
| Payload | Exatamente pitch*height bytes capturados do guest, até 1 MiB por imagem |

A instrução usa DMASK=RGBA, UNRM=1, R128=0, coordenadas inteiras X/Y em dois VGPRs consecutivos, quatro VGPRs de destino e descriptor s8..15. Opções de sampler, LOD explícito/MIP, array, TFE/LWE/cache e controles fora desse padrão são recusados. O decoder verifica definição das duas coordenadas e das oito palavras do descriptor, mais o grupo inteiro de destino; o GNM verifica esses grupos contra RSRC1. O wrapper recusa mutação dos descriptors de buffer/imagem e preserva todos os outputs em falhas.

Antes do ImageRead, o wrapper acrescenta `x < width && y < height` ao predicado EXEC. Assim o emissor não executa o fetch fora da faixa e retorna RGBA zero, inclusive para coordenadas negativas representadas em uint32. Isso não depende de robustImageAccess do host. O teste exercita separadamente x==width, y==height e UINT32_MAX.

## ABI independente por estágio

| Recurso | PS | VS |
|---|---|---|
| Buffers | set0/binding58, SRV t58..59 space0 | set0/binding60, SRV t60..61 space0 |
| Imagem | set0/binding64, SRV t64 space0 | set0/binding65, SRV t65 space0 |
| Metadata de buffers | A partir do DWORD0 | A partir do DWORD4 |

Cada estágio mantém o formato existente: um DWORD de offsets de 8 bits compactados, seguido pelos tamanhos dos buffers em bytes. Dois buffers ocupam três DWORDs. `CompileProgram` já permite selecionar o início de push data; o wrapper GCN usa essa API para reservar DWORD4 ao VS. A normalização de decorações GCN agora distingue buffers e imagens, sem afetar a política de bindings dos shaders PS5.

D3D12 usa tabela de quatro SRVs raw e tabela de duas imagens, oito DWORDs de metadata b0/space1000 e o runtime Mesa existente em space1001. As imagens são recursos Texture2D RGBA8_UNORM reais: upload por footprint, cópia de cada linha usando o pitch guest, transição COPY_DEST → PIXEL_SHADER_RESOURCE|NON_PIXEL_SHADER_RESOURCE e SRV. Buffers, imagens, uploads e heap ficam retidos até fence/device loss.

Vulkan cria imagens RGBA8_UNORM sampled, memória, views e staging distintos para VS/PS; copia com bufferRowLength=pitch/4 e transições UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY. Os descriptors são SAMPLED_IMAGE, sem sampler. O layout preserva o bloco de push constants de 128 bytes e atualiza 32 bytes de metadata. Descritores, views, imagens e memória são destruídos após conclusão da GPU, inclusive no caminho excepcional já protegido pelo probe.

## GNM e runtime

Cada operação possui snapshots independentes dos quatro buffers e das duas imagens. A captura ocorre sob a lease da memória guest. É recusado qualquer recurso de shader que se sobreponha a um draw target do lote; ainda não há render-to-texture/coerência entre draws. O orçamento agregado de snapshots é 16 MiB por lote, verificado enquanto cada draw é preparado, antes de executar GPU. A preparação inteira deve passar antes de publicar imagens ou estado.

O ELF AMD64 próprio usa os cinco imports GNM existentes. Inicializa os seis payloads na memória guest, submete um draw, muda posição/cores/modulação/coordenadas e ambas as imagens, submete outro draw menor e verifica pixels. Um lote inválido posterior deve ser recusado preservando o resultado. O teste host confere todos os 2048 pixels finais, incluindo os pixels do primeiro draw preservados fora do segundo triângulo. Nenhum dado proprietário é usado.

## Arquivos alterados

| Arquivo | Alteração |
|---|---|
| GcnCompiler.h/.cpp | Descriptor T#, especialização Float/2D, guards X/Y, bindings e push data por estágio; output de imagem transacional |
| frontend/decode/GcnDecoder.h/.cpp | MIMG IMAGE_LOAD, grupos/definição e fixtures GCN próprias |
| src/libs/ps4Gnm.cpp | Snapshots e validação de quatro buffers/duas imagens, grupos RSRC1, alias e orçamento |
| graphics/guest_gpu/ps4Raster.h | Buffers por estágio, Texture2D e validação do snapshot/pitch |
| host_gpu/d3d12/ps4RasterProbe.cpp | Recursos de imagem, uploads, SRVs e root tables/metadata independentes |
| host_gpu/renderer/ps4TransferProbe.cpp | Imagens/staging/views/descriptors/layouts e memória dos dois estágios |
| tests/Ps4TextureFixture.h | Padrões legais, pitch com padding e referência CPU independente da tradução |
| Ps4GcnShaderTests / Ps4GcnDxilTests / Ps4RasterTests | Positivos/negativos, SPIR-V/DXIL e imagem completa em três hosts |
| Ps4GnmTestFixture.h / Ps4GnmTests.cpp / CMakeLists.txt | ELF, snapshots, negativos, orçamento e novo CTest ps4_gnm_textures |
| tests/run-ps4-gcn-gpu.ps1 | Preflight das duas novas fixtures |

Os nomes abreviados seguem os diretórios existentes de graphics/shader/recompiler e tests. Não foi criado backend ou processo paralelo.

## Evidências finais

Diretório: `build/stage4-textures/`.

- **shaders-final.log:** 21 fixtures SPIR-V validadas; novos módulos VS/PS combinam IMAGE_LOAD e dois buffers. Negativos de formato, pitch, limites, campos reservados, controles/opcode/grupos, quantidade de SGPRs e mutação do descriptor preservam os outputs. `texture-vs.spvasm` / `texture-ps.spvasm` mostram OpImageFetch, guards e bindings distintos.
- **gpu.log:** 34 objetos DXIL SM6.4 ligados/validados; 59 variantes em cada host (177 readbacks) mais três cenários mistos de transferências/draws. Radeon RX 6600M D3D12 e Vulkan, além de WARP; zero erros nas camadas debug/validation. Cada cena compara os 2048 pixels RGBA com referência CPU, tolerância de um nível UNorm.
- Novas variantes 23..54 conferem os 32 texels de uma imagem 8×4 com pitch de 256 bytes e padding sentinela, usando simultaneamente quatro buffers e duas imagens. A 55 altera os dados e a geometria (cobertura 576→256); 56..58 conferem OOB X/Y/UINT32_MAX e RGBA zero. A imagem VS também tem padrão distinto por texel, afetando o gradiente exportado.
- **readback-comparison.log:** 72 comparações entre os hosts nos 36 casos novos; diferença máxima de um nível RGB. Isso não é alegação de identidade binária em todos os gradientes; a referência RGBA de cada host passou.
- **unit-final.log:** seis payloads mudam independentemente sem mudar os módulos VS/PS; descriptors/endereço/alias inválidos recusados sem renderer/escritas. Um lote de imagens maiores ultrapassa o orçamento de snapshots e é recusado antes da GPU; a submissão válida seguinte passa.
- **guest-final.log:** status600d474e, duas submissões, dois draws e uma rejeição esperada; quatro buffers e duas imagens em VS/PS, atualizações e todos os pixels finais aprovados. O primeiro run chegou ao mesmo status, mas a referência CPU não considerava os pixels preservados do draw anterior; corrigida somente a referência e repetido, preservando `guest-reference-error.log`.
- **ctest.log / ctest-details.log:** 25/25 aprovados em 7,07s, incluindo ps4_gnm_textures, modos anteriores PS4 e regressões PS5 shader_cfg/scalar_provenance/resource_tracking/materialization, plataforma/loader/HLE. Não houve gameplay PS5.
- **build-native.log:** cinco fontes de produção alterados/dependentes (frontend/GNM e hosts Vulkan) recompilados com os comandos CMake locais; quatro executáveis de testes ligados aos demais objetos do core já compilados. `relink-tests.log` / `relink-gnm-final.log` atualizam os negativos e a referência final. Não houve rebuild limpo do aplicativo desktop inteiro. `build-vulkan.log` / `link-vulkan.log` confirmam probe standalone final; `configure-desktop.log` registra a configuração CMake do novo teste.
- **build-uwp-final.log / audit-uwp-final.log:** build completo UWP/D3D12 e layout aprovados; imports WindowsApp.lib PASS. Nenhuma instalação, deploy, execução AppContainer ou Xbox. Comentários de API foram ajustados ao fim, sem mudança funcional.
- **evidence-summary.log:** checagem automática dos totais e dos resultados acima.

A primeira compilação apontou o acessor inexistente IR::Value::Inst; substituído pelo ResolveInstruction existente. Uma edição auxiliar interrompeu em assertion de espaçamento e foi reaplicada antes da compilação. Os resultados aceitos são os logs finais descritos; não houve alteração nas referências externas.

## Reprodução

Com as dependências existentes e PATH MSYS/Qt para os testes GCC, KYTY_LAZY_MEMORY=1:

```powershell
cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_gcn_shader_tests ps4_gnm_tests ps4_raster_vulkan_tests shader_cfg_tests -j4
build/stage1-desktop/ps4_gcn_shader_tests.exe build/stage4-textures/fixtures
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-textures/guest --textures
ctest --test-dir build/stage1-desktop --output-on-failure -R '^(ps4_.*|guest_platform|guest_platform_files|shader_cfg|scalar_provenance|archive_file|resource_materialization|resource_tracking|ngs2_sampler|launcher_guest_platform)$'
tests/run-ps4-gcn-gpu.ps1 -FixtureRoot build/stage4-textures/fixtures -TranslatorRoot build/stage3/spirv_to_dxil-dist -VulkanRoot C:/VulkanSDK/1.4.357.0 -OutputDir build/stage4-textures/gpu-tools
src/uwp/uwp.ps1 build -BuildDir build/stage3-uwp
build/stage3/audit-uwp-imports.ps1
```

Nesta execução, `build/stage4-textures/build-tests.py` e `relink.py` usaram comandos preservados do compdb local para reutilizar o core, evitando recompilá-lo por cada entrada de teste. O probe standalone foi recompilado pelos mesmos comandos. Esses auxiliares são artefatos locais; os targets acima são a reprodução normal em outro checkout. Nada deve ser instalado no ambiente atual.

## Referências, limites e próxima tarefa

Somente leitura: shadPS4 frontend/decode.cpp (decodeInstructionMIMG), opcodes.h/instruction.h, video_core/amdgpu/resource.h/pixel_format.h/tiling.h; SharpEmu GcnImageDescriptor.cs (T# GCN de oito DWORDs e diferenças de layout frente a RDNA2). Conferidos campos, encodings e restrições de formato/pitch. Código e fixtures são próprios, sem copiar código GPL das referências.

Suporte limitado a **um IMAGE_LOAD RGBA8 linear, um mip/layer e dois buffers read-only por estágio**, com descriptors diretos e snapshots por draw. Não há sampler/filtragem, IMAGE_SAMPLE, mips/arrays/MSAA/depth, outros formatos/tiling, image writes/atomics, render-to-texture, caches/coerência gerais ou apresentação VideoOut. O lançamento PS4 normal continua recusado; isso não comprova jogos comerciais ou Xbox. O build/teste desktop executado é Windows/Vulkan, sem build Linux neste ciclo.

Próximo incremento: descriptor de sampler PS4 nearest/clamp e IMAGE_SAMPLE_L com LOD0 explícito, ligado aos dois hosts, padrão que revele bordas/orientação e atualizações pelo GNM. Preservar a separação VS/PS e os testes atuais antes de ampliar filtragem, mips e tiling.
