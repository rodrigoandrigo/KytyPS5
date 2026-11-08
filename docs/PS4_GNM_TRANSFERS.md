# PS4 — transferências integradas à submissão GNM

Data: 2026-11-08. Estado: **CONCLUÍDO para este incremento**. Este incremento cobre parte de P9 em PS4_FASE4_PENDENCIAS.md. Não conclui fase 4, recursos gerais ou compatibilidade comercial.

## Problema e alteração

WRITE_DATA e DMA_DATA immediate-fill tinham decoder/probes próprios, mas SubmitOnWorker recusava esses opcodes. O runtime agora prepara uma sequência ordenada de transferências e draws, usando o decoder existente e o novo executor compartilhado ApplyTransfers para snapshots privados.

Todas as DCBs, shaders, estados e transferências são validados antes da primeira chamada ao renderer. O mesmo alvo atravessa a sequência fill → draw → escritas parciais → draw. Cada draw recebe os pixels atuais, executa pela GPU e retorna somente após conclusão/readback. Somente depois de toda a sequência bem-sucedida os alvos são copiados para a memória guest e o estado GNM é publicado.

## Contrato implementado

- WRITE_DATA: destino memória 5, endereço incremental e confirmação opcional, conforme DecodeTransfers existente.
- DMA_DATA: preenchimento imediato em memória, controles do subconjunto existente; ordem serial neste perfil síncrono. Não representa engine DMA assíncrona ou cópia memória→memória.
- Destinos precisam caber integralmente em um alvo de draw validado **neste lote**. O draw pode estar depois da transferência. Não há transferências gerais para shaders, descriptors, memória arbitrária ou lote sem draw.
- Mantidos os limites de draw/raster e de DCBs. Adicionados 4096 pacotes de transferência e 16 MiB de bytes escritos por lote, contando sobrescritas; máximo de 64 draws contado independentemente das transferências.
- Em lote misto, alvos não podem sobrepor código dos shaders lidos, DCBs ou arrays de endereços/tamanhos. Essa recusa protege a semântica de snapshots; não implementa self-modifying command streams.
- Vale a ordem entre pacotes de uma DCB e entre as DCBs da submissão. Não permite indiretos, CCB/CE/DE, novos eventos ou sincronização entre engines.
- Mesma lease do perfil protege lifetime dos mapas durante validação, GPU e commit. Ela não passa a ser um protocolo geral de coerência diante de escritas CPU guest concorrentes.

As transferências são materializadas **na CPU, em staging privado**; upload, blending/draw e readback usam os adaptadores GPU existentes. Não anunciar que DMA_DATA virou um novo comando de fill GPU neste caminho. Recursos continuam temporários e há fence/readback por draw. Os probes antigos de transferências GPU continuam disponíveis e separados.

## Arquivos

| Arquivo | Alteração |
|---|---|
| src/libs/ps4Gnm.cpp | Operações ordenadas, validação/resolução por alvo, budgets, aliases e commit só no sucesso |
| src/graphics/guest_gpu/ps4Transfer.h/.cpp | ApplyTransfers valida plano/span inteiro antes de alterar o snapshot; reutilizado no GNM e probes |
| tests/Ps4GnmTestFixture.h | ELF AMD64 próprio com DMA, writes e dois draws por submissão, alpha blend e verificações guest |
| tests/Ps4GnmTests.cpp | Ordem, múltiplas DCBs, budgets, aliases, rollback e conferência de pixels do ELF |
| tests/Ps4RasterTests.cpp | Sequência de staging compartilhada entre dois draws reais em cada backend |
| CMakeLists.txt | CTest ps4_gnm_transfers e ligação do executor no probe D3D12 |
| tests/run-ps4-gcn-gpu.ps1 | Compila o executor compartilhado junto do probe D3D12 |

Nenhuma alteração da tradução RDNA2/IR PS5 ou das APIs de renderer foi necessária. Os recursos/texturas e SMRD do relatório continuam pendentes.

## Testes e evidências

Diretório do ciclo: build/stage4-transfers-gnm/.

- gpu-final.log: cenário misto passou em D3D12 Radeon RX 6600M, WARP e Vulkan, conferindo todos os 2048 pixels RGBA; zero erros D3D12 debug/Vulkan validation. Os dez cenários anteriores e validação de 18 objetos DXIL também foram executados pelo script.
- unit-final.log, testes unitários aprovados: fill/write/draw intercalados, write após último draw, ordem entre duas DCBs, endereço fora/desalinhado/overflow, tamanho/controles inválidos, limite de pacotes/bytes, lote sem alvo, alvo read-only, aliases de shader/DCB/arrays, opcode desconhecido após operações válidas e erro de renderer sem publicar transferências.
- guest.log / ctest-details.log, ELF executado com sucesso: código guest chama builders/imports GNM, duas submissões válidas de dois draws cada e uma inválida; pixel sobrescrito entre draws distingue a ordem pelo blend. Conferência completa da imagem final pelo teste host, além das verificações em AMD64 guest.
- ctest-final.log: **22/22 testes aprovados**, incluindo o ELF novo e regressões PS5 de tradução/CFG, proveniência e recursos. Resultado: status 0x600d474e, duas submissões válidas, quatro draws e uma rejeição.
- build-desktop.log / build-probes.log / relink*.log: núcleo desktop compilado e testes ligados aos mesmos objetos finais de produção; ApplyTransfers foi recompilado após sua última alteração antes do link.
- build-uwp-final.log e audit-uwp-final.log: build completo UWP/D3D12 e imports contra WindowsApp.lib aprovados. Não houve execução AppContainer/Xbox, nem instalação.
- readback-comparison.log: o resultado RGB do cenário misto é idêntico em D3D12, WARP e Vulkan.

Durante a revisão, o teste de limite de bytes foi encontrado exercitando antes a proteção de alias DCB/framebuffer. Foi corrigido usando DCB grande em faixa separada e assertions do motivo de recusa; a reexecução atingiu os dois budgets esperados. Logs: unit-final.log e relink-budget-fix.log.

A garantia transacional cobre falhas de validação e execução antes do commit. O commit depende dos callbacks de memória do perfil continuarem válidos/escrevíveis sob a lease; não introduz rollback geral de múltiplos writes de callbacks arbitrários que falhem no meio.

## Reprodução

```powershell
cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_gnm_tests ps4_raster_vulkan_tests
build/stage1-desktop/ps4_gnm_tests.exe --unit-only
build/stage1-desktop/ps4_gnm_tests.exe build/stage4-transfers-gnm/guest --transfers
ctest --test-dir build/stage1-desktop --output-on-failure -R ps4_gnm
```

O script tests/run-ps4-gcn-gpu.ps1 usa fixtures existentes de build/stage4-shaders/fixtures, Mesa em build/stage3/spirv_to_dxil-dist e o SDK Vulkan já instalado. Não instala/deploya aplicativos.

## Referências e próximo incremento

Consulta somente leitura: shadPS4 src/video_core/amdgpu/liverpool.cpp, tratamento DmaData/fill; SharpEmu src/SharpEmu.GnmMode/Gpu/GnmGpuWrites.cs, distinção de bytes CPU/GPU sujos. Contratos de pacotes reutilizam o decoder próprio já existente. Não houve cópia de implementação das referências.

Próximo passo da missão: P1/P2, descriptor de buffer PS4 e s_buffer_load_dwordx4 com binding real Vulkan/D3D12 e dados alterados entre submissões. Apresentação VideoOut, memória geral de shader, CCB e engines assíncronas continuam fora deste incremento. Nenhuma alegação de execução Xbox/AppContainer ou gameplay PS5/PS4.
