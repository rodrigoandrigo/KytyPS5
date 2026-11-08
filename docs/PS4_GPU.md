# GPU PS4 — etapa 4

Estado em 2026-11-07: **transferências e draw GCN diagnóstico validados; renderização PS4 geral pendente**. Nada foi instalado. O diagnóstico não habilita jogos PS4 na interface. O incremento mais recente está em [PS4_GCN_RASTER.md](PS4_GCN_RASTER.md): frontend GCN próprio → IR → SPIR-V/DXIL → triângulo, scissor, blend e máscara de canais, nos dois backends. Os contratos e resultados de transferências abaixo continuam válidos.

## Diferenças verificadas no código local

| Área | Evidência e consequência |
|---|---|
| PM4 | `shadPS4-main/src/video_core/amdgpu/pm4_cmds.h` define o header type-3, `PM4DmaData` e `PM4CmdWriteData`; `liverpool.cpp` trata os pacotes. `KytyPS5/src/graphics/guest_gpu/pm4.h` compartilha alguns números de opcodes, mas tem extensões próprias em NOP e estado Prospero. Igualdade de opcode não prova compatibilidade de todo o pacote. |
| Registradores/pipelines | `KytyPS5/src/graphics/guest_gpu/command_processor/pm4Dispatch.cpp` instala handlers PS5, com GS/HS e user-data próprios. O frontend PS4 separado não encaminha `SET_CONTEXT_REG`, `SET_SH_REG`, draw ou dispatch a essa tabela. Esses estados ainda precisam de conversões específicas. |
| ISA | `shadPS4-main/src/shader_recompiler/frontend/opcodes.h` identifica VOP3 GCN por `0x34 << 26` e SMRD por `0x18 << 27`. O `ShaderDecoder.cpp` do Kyty identifica VOP3 RDNA2 por `0x35` nos seis bits superiores e SMEM por `0x3d`. EXP/VOP1 e algumas famílias compartilham prefixos, o que não valida todos os opcodes, operandos, wave size ou semântica. O decoder RDNA2 não é usado para shaders PS4; o novo frontend GCN normaliza somente seu subconjunto validado para a IR compartilhada. |
| Formatos/descriptors | `shadPS4-main/src/video_core/amdgpu/resource.h` contém descriptors GCN, formatos e `tiling_index`; `gpu_defs.h`, `hardwareContext.h` e `tile.*` do Kyty usam tipos Prospero. Nenhum cast entre esses descriptors foi introduzido. O diagnóstico recebe RGBA8 linear explicitamente, com pitch em bytes. |
| Clears/metadados | `shadPS4-main/src/video_core/renderer_vulkan/vk_rasterizer.cpp::EliminateFastClear` depende de estado color/CMASK. Um preenchimento DMA não foi anunciado como fast clear CB, DCC/CMASK ou draw PS4. |
| Sincronização | O subconjunto produz uma sequência ordenada de cópias host, com dependências entre sobrescritas, transições de imagem e fence antes do readback/liberação. Não implementa `WAIT_REG_MEM`, EOP/EOS, semáforos guest, caches/engines ou indireção PS4. |
| Hosts | Os adaptadores ficam nos diretórios D3D12 e Vulkan existentes. Recebem device/queue do host; não criam um backend alternativo. O alvo UWP inclui apenas o adaptador D3D12. O teste desktop cria seus próprios devices para exercitar esses mesmos adaptadores. |

A referência shadPS4 foi consultada para conferir campos e semântica; a implementação e as fixtures são próprias. Não foram incorporados seu rasterizador ou seu backend Vulkan.

## Implementação e limites

- `src/graphics/guest_gpu/ps4Transfer.*`: API independente de plataforma, sem dereference de ponteiros guest. Aceita somente type-3 graphics sem predicação e bits reservados zerados. NOP tem payload opaco; subcódigos NOP PS5 não são executados.
- `WRITE_DATA`: destino 5 (memória), endereço incremental, confirmação opcional; recusa write-one-address, outros engines e controles. `DMA_DATA`: fonte imediata 2, destino memória 0, CP_SYNC opcional, tamanho não nulo em dwords, sem swaps/endereçamento alternativo. Outros modos são recusados.
- Faixa explícita do alvo, alinhamento de dword, overflow, packet truncado, limite de 4096 escritas e orçamento de 16 MiB são verificados. O output só é substituído após validar todo o stream. Os adaptadores validam novamente planos recebidos diretamente antes de gravar comandos GPU.
- Alvo RGBA8 linear não vazio, até 16 MiB, pitch múltiplo de 256 e suficiente para a largura. Esse pitch é um contrato de transporte do diagnóstico para os dois hosts, não uma exigência geral do PS4. Padding não participa da comparação dos pixels.
- O plano usa buffers/imagem privados inicializados com zero; endereços guest são relocados para offsets desse alvo. Não espelha memória de um jogo nem executa transferências sobre seus recursos. O preenchimento imediato é materializado em staging e executado por cópias GPU, preservando a ordem de sobrescritas.
- `src/graphics/host_gpu/d3d12/ps4TransferProbe.*`: upload → buffer default → textura RGBA8 → readback. Recursos são mantidos até conclusão da fence/dispositivo perdido. Integrado a `RunShaderSelfTest`, já disponível no aplicativo; compilar a integração não significa ter executado o aplicativo UWP.
- `src/graphics/host_gpu/renderer/ps4TransferProbe.*`: mesma operação em Vulkan, usando funções do device já existente, barreiras de transferência e visibilidade host. O chamador serializa a queue. Não requer Vulkan no executável UWP/D3D12.

## Validação

`tests/Ps4TransferTests.cpp` cobre recusa de pacotes truncados, controles não suportados, predicação, endereços fora da faixa/desalinhados, overflow do alvo, opcode desconhecido após comandos válidos (sem alterar output), NOP e planos forjados. A fixture gráfica usa `DMA_DATA` para preencher e `WRITE_DATA` para sobrescrever regiões em ordem; não é apenas um upload de imagem pronta.

Os testes GPU conferem todos os pixels em 64×32/pitch 256, 65×17/pitch 512 e 1×1/pitch 256. A segunda dimensão verifica padding e divisão ímpar. Dispositivos observados:

- D3D12: AMD Radeon RX 6600M e Microsoft Basic Render Driver/WARP. Camada de depuração habilitada, zero erros. `build/stage4/d3d12.log`.
- Vulkan: AMD Radeon RX 6600M. `build/stage4/vulkan.log`. Essa execução inicial não habilita camadas de validação Vulkan.
- As imagens 64×32 são byte a byte iguais: `build/stage4/d3d12.png` e `build/stage4/vulkan.png`. SHA-256 dos pixels RGB: `84e689b8d4fb607658446163d875480601f3ee617493b15b973c261f66144e04`.
- Build completo UWP/D3D12 aprovado: `build/stage4/build-uwp-final.log`. Imports nomeados do layout aprovados contra `WindowsApp.lib`: `build/stage4/uwp-import-audit.log`. A auditoria não cobre imports por ordinal nem comportamento no Xbox.

Regressões: 13 testes CTest aprovados em `build/stage4/ctest.log` (novos decoder/Vulkan, os oito testes de CPU/HLE/plataforma anteriores e `scalar_provenance`, `resource_materialization`, `resource_tracking`); mais o novo D3D12 em `build/stage4/ctest-extra.log` e um teste de shader blit/mesh em `build/stage4/gpu-blit.log`. Foram adicionadas as dependências reais de `HostShaderFeatures.cpp` que faltavam nos dois primeiros alvos de regressão de shaders. Isso não é validação de gameplay/visual PS5 nem execução integral de `shader_cfg`.

Alvos CMake: `ps4_transfer_tests` (somente decoder), `ps4_transfer_d3d12_tests` (Windows desktop), `ps4_transfer_vulkan_tests` (desktop Vulkan com loader disponível). Os dois últimos exigem GPU/driver ou WARP e têm timeout de 60 s no CTest. As imagens podem ser exportadas passando um caminho `.ppm` ao executável de teste GPU. Nenhum desses testes instala ou registra o aplicativo.

## Trabalho restante para concluir a etapa

1. Decoder/validação de registradores e estados GCN, conversão explícita de recursos/formatos e tiling.
2. Expandir o frontend ISA GCN validado para shaders de jogos, com memória, fluxos, interpolação e controles FP, preservando RDNA2/PS5.
3. Expandir o draw diagnóstico para recursos/descriptors, fetch, depth/stencil, múltiplos targets e estados GCN reais, nos dois backends.
4. Integração GNM/video-out ao runtime CPU/HLE, respeitando memória, lifetimes e erros de comandos não implementados.
5. Exercitar a integração UWP e o diagnóstico no Xbox Series X/S quando houver dispositivo. SM 6.4 e falta de mesh/NGG e de certas operações nativas continuam restrições do backend; nenhuma tradução PS4 aqui contorna essas restrições.

O resultado atual inclui **transferências e triângulo produzido por shaders GCN**. Isso não demonstra submissão gráfica de jogos, gameplay ou compatibilidade comercial e não conclui sozinho a etapa 4 geral.
