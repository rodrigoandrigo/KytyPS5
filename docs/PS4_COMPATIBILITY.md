# Compatibilidade PS4 — estado e auditoria

CMP/CMPX GCN aceita SCC, EXECZ e VCCZ como fontes e valida a definição das condições. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

CMP/CMPX GCN agora aceita constantes de 64 bits por tipo e fontes de máscara EXEC/VCC. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

O frontend GCN valida salvamento e restauração independentes de EXEC_LO/HI. O readback passou nas duas saídas em D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

O frontend GCN rastreia EXEC de entrada salvo por MOV e permite escritas diretas com restauração comprovada. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

O readback confirmou as duas saídas restauradas de um CFG GCN com escrita VALU divergente em D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

CMP/CMPX VOP3 com destino explícito foi validado por readback em D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

VOPC F16 e CLASS_F64 passaram pelos casos numéricos de readback em D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

O frontend GCN ganhou operações sobre as metades de EXEC/VCC e novos predicados VOPC. A preservação de lanes nas fixtures passou por readback D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

VOPC I64/U64, cópias de máscaras B64 e laço sob EXEC reduzido validados em 2026-11-09. O readback verificou preservação de lanes inativas em D3D12, WARP e Vulkan. [Escopo e evidências](PS4_LUMEN_INTEGRATION.md).

Em 2026-11-09, o frontend GCN passou a verificar arestas de retorno no CFG. O laço escalar de teste passou no validador SPIR-V, com 10/10 testes selecionados e build UWP/D3D12 aprovados. [Limites e evidências](PS4_LUMEN_INTEGRATION.md).

Em 2026-11-09, o frontend GCN ganhou desvios VCCZ/VCCNZ e EXECZ/EXECNZ para frente e cinco operações B64 entre pares SGPR. CTest 10/10 e build UWP aprovados. Loops e manipulação geral de máscaras permanecem pendentes. [Contrato e evidências](PS4_LUMEN_INTEGRATION.md).

Validação atual (2026-11-08): perfis da integração Lumen passaram em10/10 testes selecionados e270 readbacks D3D12/WARP/Vulkan; [evidências e limites](PS4_LUMEN_INTEGRATION.md). Build UWP/D3D12 e auditoria de imports aprovados; sem execução Xbox. P0–P15 continua incompleto; resultados abaixo marcados como não validados são históricos.

Validação liberada pelo usuário em 2026-11-08: o adiamento de testes registrado abaixo foi revogado. Reconstrução e testes dos perfis limitados concluídos; resultados novos em `PROGRESS.md` e `build/lumen-validation`. P0–P15 continua incompleto.

Implementação em andamento 2026-11-08: [checkpoint Lumen](PS4_LUMEN_INTEGRATION.md) registra adaptações de SMRD/ALU, sampler e diagnóstico GNM **ainda não compiladas ou testadas**. Integração P0..P15 INCOMPLETA; testes adiados até a integração solicitada estar completa. As aprovações anteriores abaixo não validam este novo código.

Auditoria documental 2026-11-08: [contribuições do Lumen para a fase 4](PS4_LUMEN_GPU_AUDIT.md) mapeia samplers/ISA, recursos/layouts, coerência/sincronização, VideoOut, testes e limites de portabilidade/licença. Nenhum código Lumen foi integrado ou executado; as capacidades PS4 do Kyty permanecem as validadas abaixo.


Atualização 2026-11-08: [IMAGE_LOAD RGBA8 e recursos VS/PS simultâneos](PS4_TEXTURES.md) validados: até dois buffers e uma textura por estágio, ELF com seis payloads, 25/25 testes, Vulkan/D3D12/WARP e build UWP aprovados. Samplers/filtragem, demais formatos/tiling e fase 4 geral permanecem incompletos.


Atualização 2026-11-08: [MUBUF indexado, dois buffers e vertex fetch](PS4_MUBUF.md) implementados e validados no contrato limitado: 24/24 testes, ELF guest com índice/dados mutáveis, Vulkan/D3D12/WARP e build UWP aprovados. Texturas/MIMG, buffers simultâneos VS+PS e a fase 4 geral continuam incompletos.


Atualização 2026-11-08: [interpolação e cobertura GCN](PS4_GCN_COVERAGE.md): mapa smooth PARAM e interpolação linear central integrados ao GNM; sete VOP1 adicionais exercitadas com buffer GPU real. 23/23 testes, Vulkan/D3D12/WARP e build UWP aprovados nos níveis documentados. Todas as famílias GCN e a fase 4 continuam incompletas.


Atualização 2026-11-08: [descriptor e SMRD com buffer GPU real](PS4_GCN_BUFFERS.md) validados no contrato inicial de um buffer read-only no PS, com offset imediato. ELF guest altera os dados entre submissões; 23/23 testes, probes Vulkan/D3D12/WARP e build UWP aprovados nos níveis documentados. P1/P2 gerais e fase 4 continuam parciais.

Incremento validado em 2026-11-08: [transferências no GNM](PS4_GNM_TRANSFERS.md) integra WRITE_DATA/DMA_DATA immediate-fill aos draws, limitado aos alvos atuais e com staging síncrono. ELF guest e 22/22 testes aprovados; Vulkan/D3D12/WARP e build UWP verificados nos níveis descritos no relatório. Recursos gerais e apresentação continuam pendentes.

Plano da fase 4: [pendências, partes e dependências](PS4_FASE4_PENDENCIAS.md), análise de 2026-11-08 baseada no código e nas evidências atuais.

Atualização de shaders em 2026-11-08: implementados exports PARAM, interpolação perspectiva VS→PS e ALU VOP3 limitada, integrados ao estado GNM e exercitados em GPU. Contrato atual e limitações em [PS4_GNM.md](PS4_GNM.md) e evidências em PROGRESS.md. Shaders gerais completos, buffers e texturas continuam pendentes. Os relatos anteriores abaixo preservam os resultados de cada incremento.

## Incremento GNM — 2026-11-08

Implementados cinco exports GNM no runtime diagnóstico, captura e validação de DCBs/registradores PS4, SGPRs e branches escalares para frente, preservação de alvos entre draws e adaptadores Vulkan/D3D12. O ELF AMD64 próprio passou pela GPU real: duas submissões, dois draws e um lote inválido recusado. Contrato e limitações em [PS4_GNM.md](PS4_GNM.md). Shaders gerais de jogos, texturas, interpolação e video-out continuam pendentes.


As etapas abaixo registram entregas sucessivas; o estado mais recente inclui o incremento gráfico da etapa 4. As limitações das etapas anteriores são históricas.

## Etapa 4 — transferências, shaders GCN e draw (2026-11-07, parcial)

Frontend PM4 PS4 separado, plano compartilhado validado e diagnóstico de imagem com execução/transferência/readback real em D3D12 e Vulkan. Foram conferidos todos os pixels nas dimensões 64×32, 65×17 com padding e 1×1; o D3D12 também passou em WARP e com zero erros da camada de depuração. Build UWP/D3D12 e imports aprovados. Nenhuma instalação ou execução UWP/Xbox.

O incremento seguinte acrescenta um frontend GCN próprio e a ABI VS `v0`, reutilizando a IR e a emissão SPIR-V apenas para o subconjunto validado. VS/PS GCN geram um triângulo real, com scissor, blend e máscara de canais, em D3D12 hardware/WARP e Vulkan. Nove readbacks aprovados, imagens iguais byte a byte e zero erros nas camadas de validação. Quatro módulos SPIR-V, três pipelines ligados DXIL e dezessete testes CTest (incluindo `shader_cfg` PS5) aprovados. Build UWP/D3D12 e imports aprovados; nenhuma instalação ou execução AppContainer/Xbox.

O diagnóstico usa recursos privados, alvos e estados explícitos. Não implementa shaders arbitrários, descriptors/tiling PS4 completos nem submissão GNM a partir de jogos. O teste comprova rasterização desse subconjunto e preservação dos testes PS5; não comprova gameplay ou boot comercial. Contratos e evidências: [PS4_GPU.md](PS4_GPU.md) e [PS4_GCN_RASTER.md](PS4_GCN_RASTER.md).

## Etapa 3 — runtime PS4 de CPU/HLE (2026-11-07)

O perfil experimental conecta o adaptador da etapa 2 ao `RuntimeLinker`, à memória guest, ao filesystem e às threads já existentes. A execução usa um `Program` registrado, endereço-base `0x400000`, entry point pelo `RunEntry` e stack guest. O perfil é habilitado explicitamente na CLI com `--ps4-runtime`; a biblioteca/UI mantém a recusa de jogos PS4 até existir um caminho gráfico validado.

### Implementação e contratos

- `src/libs/ps4Kernel.*`: registro exclusivo PS4, com 15 exports `libkernel` v1 / módulo `libkernel` v1.0. São wrappers SysV sobre os serviços existentes; nenhum catálogo PS5 é incorporado automaticamente. Import obrigatório desconhecido ou com identidade/versão diferente impede a carga, com diagnóstico do adaptador.
- `RuntimeLinker::LoadPs4Program` / `ExecutePs4`: validação, registro, init/entry/fini, espera de workers, limpeza e unload existentes. Uma imagem principal por sessão; execução repetida do mesmo `Program` é recusada. Init/fini usam a assinatura de módulo do runtime existente e exigem retorno zero neste subconjunto. Segmentos separados e alinhados a 16 KiB; RWX é recusado. A imagem é preenchida RW e publicada com as proteções finais; os helpers TLS são publicados executáveis separadamente.
- Memória: `sceKernelMapNamedFlexibleMemory` aceita alocações RW, flags zero e endereço de entrada nulo, em múltiplos de 16 KiB; `sceKernelMunmap` exige exatamente uma alocação pertencente à sessão. Limite acumulado de 64 MiB neste perfil de teste. Não representa o orçamento de memória do PS4.
- Arquivos: `sceKernelOpen/Close/Read/Write`, até 64 descritores; acesso de leitura em `/app0`, leitura/escrita em `/data`. Flags de acesso, criação e truncamento; outras flags retornam ENOTSUP. Diretórios retornam EISDIR. Caminhos fora dessas montagens, traversal, componentes com ponto/espaço final, caracteres/nomes de dispositivo reservados do Windows e links/reparse points observados são recusados. São verificações de compatibilidade de caminhos; código guest nativo não é isolado como processo seguro.
- Ponteiros e handles: os wrappers verificam faixas e permissões guest antes de ler/escrever; não aceitam descritores, threads ou mutexes alheios à sessão. Ponteiro inválido retorna EFAULT; descritor inválido, EBADF; escrita em `/app0`, EROFS. Strings UTF-8 são verificadas e limitadas. I/O por chamada limitado a 64 MiB.
- Threads: `scePthreadCreate/Join` com atributos padrão, até 16 workers simultâneos. Entrada em código guest executável e stack pelos mecanismos existentes. O encerramento aguarda os workers antes de descarregar o código, incluindo os que não foram unidos pelo programa.
- Sincronização: `scePthreadMutexInit/Lock/Unlock/Destroy`, atributos padrão e até 64 mutexes. O lock de controle da sessão é liberado antes de esperar um mutex ou unir uma thread.
- TLS: `__tls_get_addr` para o módulo principal (ID 1), template e zero fill por thread, índice limitado ao tamanho TLS declarado. O tamanho estático é arredondado a pelo menos 32 bytes para alinhar o TCB do runtime existente. Este perfil aceita alinhamento TLS até 32 bytes. O patch existente de leitura `mov reg, fs:[0]` também atende o programa PS4; o patch PS5 que elimina stores FS:[0x28] não é aplicado ao PS4. Não é implementação geral de FS/GS/DTV ou TLS de PRX externo.
- Temporização: `sceKernelGetProcessTime` e `sceKernelUsleep` reutilizam os serviços existentes. Não há polling de GPU nem inicialização de janela, áudio ou controle neste perfil.
- Encerramento: depois de init/entry/fini e join, fecha arquivos, desmonta `/data` e `/app0`, libera mapas flexíveis e destrói mutexes restantes. Erro de limpeza é propagado. Falha irrecuperável ao unir uma thread impede liberar código ainda em uso. A stack principal é reutilizada pelo pthread principal existente e liberada no encerramento do subsistema de memória; isso não implementa LoadExec/reinicialização da aplicação.

O perfil conserva a infraestrutura e o orçamento global de memória existentes; não implementa ainda um orçamento de hardware PS4. Os testes usam `KYTY_LAZY_MEMORY=1` para evitar commit antecipado da memória física do emulador.

A CLI usa `_Ps4Data/diagnostic` no diretório de execução como `/data`. É uma pasta de diagnóstico compartilhada, não uma implementação de saves por título. Exemplo: `kyty_emulator --game <pasta-da-fixture> --ps4-runtime`. Sem essa opção, a recusa PS4 permanece. Nenhum novo processo ou serviço externo é necessário.

As identidades/NIDs e assinaturas foram conferidas no código local de `shadPS4-main/src/core/libraries/kernel/{file_system.cpp,memory.cpp,time.cpp,threads/pthread.cpp,threads/mutex.cpp,threads/tcb.cpp}` e comparadas aos serviços do KytyPS5. Não houve cópia de implementação dessas referências.

### Teste concreto

`tests/Ps4RuntimeTests.cpp` gera ELF e SELF descriptografado redistribuíveis, com código AMD64 próprio, imports reais do perfil e init/fini. O teste executa o guest; não substitui suas chamadas por uma sequência de operações host. Verifica permissões RX/RW, leitura de quatro bytes de `/app0/input.bin`, gravação efetiva em `/data/result.bin`, erros de ponteiro/caminho/escrita, thread bloqueada em mutex e TLS independente. Acesso TLS é exercitado tanto pelo import quanto pela leitura FS:[0] remendada pelo runtime. Confere a stack guest, init/fini, unload e rejeição de segunda execução. Uma terceira execução ELF deixa arquivo, mapa, mutex e worker para a limpeza automática.

### Resultado validado

**Critério de aceitação atendido no Windows desktop x64.** Oito testes aprovados em 1,38 s: `ps4_runtime`, `ps4_runtime_profile`, `ps4_module`, `guest_platform`, `guest_platform_files`, `archive_file`, `ngs2_sampler` e `launcher_guest_platform`. Log integral: `build/stage3/ctest-final.log`.

```text
PS4_RUNTIME: ELF status=0x600dcafe last_return=0x0 reads=1 writes=1 threads=1/1 TLS=3
PS4_RUNTIME: SELF status=0x600dcafe last_return=0x0 reads=1 writes=1 threads=1/1 TLS=3
PS4_RUNTIME: ELF-auto-cleanup status=0x600dcafe last_return=0x0 reads=1 writes=1 threads=1/1 TLS=3
100% tests passed, 0 tests failed out of 8
```

`ps4_runtime_profile` exercita o caminho de aplicação `Emulator::Run` com detecção automática PS4 e opção CPU/HLE; não é um teste do parser de argumentos nem de uma janela. Confere a gravação real dos quatro bytes. O teste do carregador também verifica TLS com 16 bytes declarados e 32 bytes de armazenamento para o TCB, preservando o limite declarado.

O alvo completo `ps4_runtime_tests` foi compilado com GCC 16.1.0/Ninja e todos os fontes do núcleo, incluindo `emulator.cpp` e o renderizador Vulkan existente. Log: `build/stage3/build11.log`. Depois dos ajustes encontrados na execução, os quatro fontes alterados (`runtimeLinker.cpp`, `ps4Kernel.cpp`, `memory.cpp` e o testador) foram recompilados com seus comandos gerados pelo CMake/Ninja e o executável foi religado; logs `build/stage3/rebuild-*.log` e helper local `rebuild_runtime.py`. Foram removidas apenas informações de debug do executável de teste durante essa ligação para reduzir o custo de carregamento; isso não altera o código guest ou os asserts. O executável normal `kyty_emulator` e a UI completa não foram reconstruídos/exercitados nesta etapa. `main.cpp` passou pela verificação de compilação sem geração de objeto (`build/stage3/main-syntax-final.log`).

Para destravar esse build, foram necessárias correções de integração anteriores ao perfil PS4: tipos explícitos em 28 inicializadores/atribuições Vulkan de 13 arquivos (mesmos valores), membro inicial padrão em `Ngs2RackOptionUnion`, restauração de `max` somente nos fontes C de `cpuinfo` e dependências Windows `ws2_32`/`bcrypt` após os arquivos estáticos do FFmpeg. O teste existente `ngs2_sampler` passou. Isso verifica compilação e o teste de áudio, não regressão visual ou gameplay PS5.

Os erros iniciais do teste foram corrigidos antes da aprovação: a imagem `Program` usa memória privada RW, e os helpers de backing físico não servem para copiá-la; leitura de arquivo sem renderizador precisa dispensar somente a invalidação de cache GPU, mantendo commit/pin da memória; o gerador da fixture deve emitir chamadas indiretas com o operando aceito pelo Xbyak. A fixture agora verifica o erro de geração antes de executar e compara resultados guest, efeitos em disco e contadores, evitando aceitar um simples retorno do entry point como sucesso.

Comandos de reprodução em uma árvore configurada com as dependências já disponíveis:

```powershell
cmake --build build/stage1-desktop --target ps4_runtime_tests ps4_module_tests guest_platform_tests guest_platform_file_tests archive_file_tests launcher_guest_platform_tests ngs2_sampler_tests -j 6
ctest --test-dir build/stage1-desktop -R '^(ps4_runtime|ps4_runtime_profile|ps4_module|guest_platform|guest_platform_files|archive_file|launcher_guest_platform|ngs2_sampler)$' --output-on-failure -j 1
```

O CTest define `KYTY_LAZY_MEMORY=1` para os dois testes do runtime. Para gerar uma fixture própria sem executá-la: `ps4_runtime_tests --emit-fixture <pasta>`; o programa cria `app0/eboot.bin`, `app0/sce_sys/param.sfo` e `app0/input.bin`.

### Limites

O critério desta etapa é o teste básico de CPU/HLE, não boot comercial. Não há GPU PS4, imagem apresentada, áudio, input, eventos, API de save data, POSIX completo, dependências guest recursivas, TLS externo, PRX dinâmico ou LoadExec implementados neste incremento. Recursos adicionais devem ser adicionados quando houver teste que exercite seus contratos. Import ausente continua sendo bloqueio explícito; não existe stub universal de sucesso.

A tentativa inicial de configuração UWP parou por ausência de `spirv_to_dxil` (`build/stage3/configure-uwp.log`). Esse bloqueio foi resolvido compilando os fontes de `C:/Users/rodri/Dev1/Projetos/Mesa-UWP`, revisão `e1d9d2b601230c490ed803776a67199e06d87243`, em cópia separada. O checkout original permanece intacto. A distribuição está em `build/stage3/spirv_to_dxil-dist`, com revisão, hashes e ferramentas em `manifest.json`; o patch cumulativo está em `3rdparty/spirv_to_dxil/mesa-uwp-e1d9d2b601.patch`.

O port mantém a API de pipeline ligado do Kyty e explicita a diferença entre os 36 bytes de dados de vértice enviados pelo backend e os 200 bytes da estrutura estendida do fork. O modo compacto verifica tamanho e offsets, mantém a alocação de root constants e rejeita acessos além do prefixo. O modo padrão do Mesa conserva os 200 bytes. Não foram implementados campos adicionais no backend por essa adaptação.

Cinco objetos DXIL passaram no validador do SDK: compute, VS/PS ligados e geometria com ABI compacto/completo. O caso que requer viewport adicional é recusado no modo compacto pelo motivo esperado. Log: `build/stage3/spirv-smoke-repro.log`. O patch passa em `git apply --check` no Mesa original; a DLL passou na conferência de imports contra `WindowsApp.lib` (`build/stage3/mesa-import-audit.log`). O preflight também recusa a pasta contendo apenas fontes, antes de criar o build (`build/stage3/source-preflight.log`).

A configuração e o build completo UWP/D3D12 passaram usando WinUI 2.8.7 e WebView2 1.0.2849.39 de `teste1`, winpthread UCRT existente, FFmpeg local e cópia local do ZArchive com o patch UWP já existente. Foram gerados `build/stage3-uwp/kyty_uwp.exe` e o layout local `build/stage3-uwp/uwp-layout`; os novos arquivos CPU/HLE PS4 também compilaram nesse alvo. Logs: `build/stage3/configure-uwp-mesa3.log` e `build/stage3/build-uwp-final.log`. Os imports nomeados do layout passaram na conferência contra `WindowsApp.lib` (`build/stage3/uwp-import-audit.log`); essa conferência não verifica imports por ordinal e não substitui execução no dispositivo. Não há validação AppContainer, Xbox Series X/S, GPU, gameplay ou regressão visual PS5. Nada foi instalado ou implantado.

---

## Etapa 2 — subconjunto de carregamento PS4 (2026-11-07)

O adaptador de carregamento PS4 está implementado e exercitado por uma fixture própria em ELF e SELF. O teste materializa segmentos, resolve um import com identidade/versões explícitas, aplica relocations e executa código AMD64 em memória no desktop. Isso não representa boot de um jogo nem execução pelo fluxo completo do aplicativo.

### Alterações

- `src/loader/ps4Module.h`, `.cpp`: adaptador PS4 que reutiliza as estruturas ELF e o `SymbolDatabase` do KytyPS5. Recebe bytes do contêiner, endereço-base, registro explícito de exports PS4 e ID TLS; retorna uma imagem relocada com segmentos, entry point, listas de inicialização/finalização e template TLS. A preparação é transacional sobre memória própria: não escreve nos endereços guest nem executa código.
- `RuntimeLinker::PreparePs4Program` em `src/loader/runtimeLinker.*`: entrada de arquivo pelo `Common::File` existente (incluindo seus caminhos virtuais de arquivo compactado), leitura limitada e diagnóstico. `Ps4Symbols()` expõe um registro separado; não copia exports PS5 nem procura NIDs no catálogo PS5 como fallback.
- `tests/Ps4ModuleTestFixture.h`, `Ps4ModuleTests.cpp` e alvo CMake `ps4_module_tests`: fixture redistribuível criada nesta entrega, sem jogo, SDK proprietário ou módulo de sistema. Os outros testes permanecem nos caminhos da etapa 1.

O adaptador não é um segundo emulador: não tem backend gráfico, CPU, gerenciador de memória ou processo próprios. A instalação definitiva da imagem no gerenciador de memória guest e a integração com os serviços do runtime permanecem como trabalho de conexão ao perfil PS4.

### Contrato suportado

| Área | Subconjunto implementado |
|---|---|
| Formato | ELF64 little-endian x86-64, FreeBSD ABI 0, ET_SCE_DYNEXEC/ET_SCE_DYNAMIC; SELF PS4 descriptografado e sem compressão, com payload por program header. |
| Limites | Arquivo e imagem de até 64 MiB cada; TLS de até 1 MiB; base alinhada a 16 KiB e às exigências dos segmentos. São limites deste incremento, não limites do hardware PS4. |
| Segmentos | PT_LOAD e PT_SCE_RELRO materializados, BSS zerado; limites, alinhamento, sobreposição e payloads conferidos. Não há acesso a memória guest na preparação. |
| Símbolos | Imports `NID#library#module`, identidades e versões SCE_IMPORT_LIB/SCE_NEEDED_MODULE, resolução por `SymbolDatabase::Find`. Sem fallback apenas por NID. Strong ausente é erro; weak ausente permanece zero. |
| Relocations | R_X86_64_64, GLOB_DAT, JUMP_SLOT, RELATIVE, DTPMOD64 (módulo atual), DTPOFF64 e TPOFF64 locais. GLOB_DAT/JUMP_SLOT usam o endereço do símbolo sem somar addend. Destinos precisam estar em segmento gravável e não podem se repetir. Tipos não implementados e overflow produzem erro. |
| TLS | Imagem inicial, zero fill, alinhamento, tamanho estático e relocations do módulo atual. O ID é fornecido pelo chamador; não há instalação de FS/GS, DTV ou TLS de threads nesta etapa. |
| Inicialização | DT_INIT presente com offset zero é distinto de tag ausente; arrays preinit/init são verificados após relocations. Fini arrays são ordenados ao contrário, seguidos de DT_FINI. |
| PRX | Preparação de ET_SCE_DYNAMIC em base fornecida pelo chamador; entry point zero é tratado como ausente. Não há carregamento recursivo de dependências nem unload no runtime completo. |
| Rejeições | PS5, imagens fixas ET_SCE_EXEC, tabelas genéricas de relocations/símbolos, REL/RELR, DT_NEEDED de módulos externos, text relocations e TLS externo não suportados pelo subconjunto. Não são substituídos por sucesso fictício. |

### Evidência de execução e limites de integração

A fixture usa dois segmentos RX/RW, import fictício `AAAAAAAAAAA` da biblioteca `libSceLoaderTest` v1 e módulo `libSceLoaderModule` v1.0. O teste usa o alocador/protetor de memória virtual já existente (`Common::VirtualMemory`), reserva a base `0x400000`, copia a imagem preparada como RW e publica o código RX antes de executar. Não há página RWX no teste.

O inicializador nativo retorna 7. O entry point em `0x400100` encaminha `Loader::EntryParams` e um callback SysV ao import; o callback de teste é chamado e o retorno diagnóstico é 42. Essa chamada usa a stack do testador; não valida a stack guest, exceções, o mecanismo `RunEntry` completo, threads ou o protocolo de término de um jogo real.

```text
PS4_ENTRY: container=ELF base=0x400000 entry=0x400100 imports=1 tls=32 return=42
PS4_ENTRY: container=SELF base=0x400000 entry=0x400100 imports=1 tls=32 return=42
Ps4ModuleTests: PASS
```

Os testes também conferem erros de formato/segmentos/tabelas, índices e versões de imports, weak imports, relocations desconhecidas ou em código, duplicidade de destinos/payloads, underflow, TLS inválido, SHN_COMMON, ordem de init/fini arrays e layout de PRX. O registro vazio não consegue resolver o import da fixture. Não houve adaptação de código externo; referências foram usadas para comparação de contratos com o código existente.

`PreparePs4Program` foi verificado sintaticamente com as opções do build desktop. Não foi exercitado como chamada do runtime completo: ele retorna a imagem preparada e não adiciona um `Program` a `m_programs`, não chama `LoadProgram`/`Execute` e não inicia serviços. O teste nativo exercita diretamente o mesmo adaptador `PreparePs4Module`. A recusa antecipada de PS4 na interface e na CLI permanece, assim como o bloqueio antigo do caminho PS5; não há alegação de execução de jogos PS4.

### Verificação e bloqueios

- Desktop Windows x64, GCC 16.1.0: `ps4_module_tests` compilou e passou com execução nativa ELF/SELF. O alvo usa `onecore` no Windows para os helpers de memória existentes.
- Suíte focada: cinco testes aprovados (`ps4_module`, `guest_platform`, `guest_platform_files`, `archive_file`, `launcher_guest_platform`). Logs em `build/stage2/ctest.log`, `native.log`, `build-final.log` e `runtime-linker-syntax.log`.
- Build completo desktop continua sem validação por causa dos erros Vulkan já registrados na etapa 1; o renderizador não foi alterado nesta etapa. Não foi reexecutado gameplay PS5.
- UWP: a nova tentativa passou pela verificação de WinUI 2.8.7 e WebView2 1.0.2849.39 em `teste1`, com o winpthread UCRT existente. Parou porque não há caminho configurado para uma distribuição utilizável de `spirv_to_dxil` (`KYTY_SPIRV_TO_DXIL_ROOT`). Isso substitui a hipótese anterior de WinUI ausente: os pacotes existem fora do caminho padrão. Log em `build/stage2/configure-uwp.log`.
- Nenhum pacote, aplicativo ou framework foi instalado e não houve deploy. Os dois projetos de referência permaneceram sem alterações.

```powershell
cmake --build build/stage1-desktop --target ps4_module_tests guest_platform_tests guest_platform_file_tests archive_file_tests launcher_guest_platform_tests -j 6
ctest --test-dir build/stage1-desktop -R '^(ps4_module|guest_platform|guest_platform_files|archive_file|launcher_guest_platform)$' --output-on-failure -j 1
src/uwp/uwp.ps1 configure -BuildDir build/stage2-uwp -WinUIRoot C:/Users/rodri/Dev1/Projetos/teste1/microsoft.ui.xaml.2.8.7 -WebView2Root C:/Users/rodri/Dev1/Projetos/teste1/microsoft.web.webview2.1.0.2849.39
```

### Próximo incremento

Na etapa 3, conectar a imagem verificada ao ciclo de vida `Program`/memória/threads do núcleo, instalar TLS e o registro HLE PS4 mínimo exigido por uma fixture de runtime. Só então habilitar um caminho PS4 de lançamento; manter bloqueio explícito para imports/dependências ainda indisponíveis. Ligar duas imagens guest com dependências, ABI de processo completa, exceções e unload ainda não foi validado.

## Etapa 1 — detecção e seleção de perfil (2026-11-07)

Implementação nesta entrega: identificação explícita PS4/PS5/desconhecido, metadados e seleção de perfil. PS4 continua sem carregamento/runtime validado e é recusado antes de inicializar os serviços PS5. As seções de auditoria abaixo são o retrato anterior da etapa 0, não o estado atualizado dos arquivos modificados.

### Componentes implementados

- `src/common/guestPlatform.h`, `.cpp`: tipo de plataforma, sondagem limitada de ELF/SELF, leitura de PSF/SFO, resolução automática/manual e decisão de permitir inicialização. A sondagem exige ELF64 little-endian x86-64, OSABI FreeBSD, tipo de console e ABI reconhecida; ABI 0 isolada não basta. Confere tabelas, offsets, tamanhos e overflow, e rejeita payloads SELF comprimidos/criptografados. Isso não substitui a validação completa do carregador da etapa 2.
- `src/common/guestPlatformFile.cpp`: adaptador sobre `Common::File`, para pastas e caminhos virtuais `.zar`, sem ler o executável inteiro. Metadados são limitados a 1 MiB. `param.sfo` fornece TITLE, TITLE_ID e APP_VER; JSON PS5 mantém escolha do título localizado e versão. Strings SFO são verificadas como UTF-8 antes de chegar à interface.
- Precedência: o executável validado define a plataforma. Metadados não resgatam um executável ausente ou inválido. `param.json` com executável PS4, `param.sfo` com PS5 ou os dois arquivos presentes produzem erro explícito. Metadados ausentes não impedem identificação por um executável reconhecido.
- `src/emulator.h`, `.cpp`: perfil solicitado em `RunOptions`, inspeção e recusa antes de `Init`, retorno de erro em vez de iniciar o runtime incompatível. O perfil selecionado PS5 chega a `ConfigOptions::guest_platform` e pode ser consultado por `Config::GetGuestPlatform()` nos serviços/runtime/GPU. O registro HLE PS4 e sua execução permanecem para as etapas seguintes.
- `src/main.cpp`: `--platform auto|ps4|ps5`; seleção manual conflitante ou executável inválido é recusado. São registrados perfil detectado, solicitado e selecionado. A ausência da opção preserva seleção automática.
- Launcher Qt: a biblioteca exibe plataforma e lê metadados PS4; a configuração permite seleção automática/PS4/PS5, persiste o pedido pelos mecanismos existentes e o encaminha pela CLI. A cópia de metadados não sobrescreve o pedido manual; a cópia de configurações por jogo o preserva.
- UWP: biblioteca exibe plataforma e persiste `platform` e `platform_error` em `library.json`; caches antigos sem plataforma são reinspecionados sem perder título/ID/versão já salvos quando o jogo está offline. O lançamento reinspeciona o arquivo, sem confiar no cache. Entradas recusadas usam a página de erro existente, mantendo navegação por controle. Developer → Testing → Game platform fornece a seleção manual; `guest_platform` é uma opção diagnóstica global em `kyty-uwp.json`, padrão `auto`. A integração UWP ainda exige validação de build e dispositivo.

### Verificação da etapa 1

- `GuestPlatformTests`: ELF/SELF PS4 e PS5 sintéticos; truncamentos, tabelas/segmentos inválidos, arquitetura/ABI desconhecidas, conflitos SELF/ELF, falhas de leitura, override, bloqueio de execução PS4 e parser SFO.
- `GuestPlatformFileTests`: inspeção em pastas e arquivos `.zar` realmente produzidos por `Common::PackArchive`, títulos/IDs/versões PS4/PS5, metadados conflitantes ou inválidos, ausência de executável e tentativa de escapar do diretório.
- `LauncherGuestPlatformTests`: valor automático padrão, separação entre detecção e pedido, cópia por jogo, persistência/recuperação em QSettings, configurações antigas sem o novo campo e importação inválida.
- Teste existente `archive_file`: regressão do acesso a arquivos compactados compartilhado pelas bibliotecas e pelo emulador.
- Os quatro testes acima passaram no desktop Windows/GCC 16.1.0, com Qt 6.11.2 para o teste do launcher. Logs locais: `build/stage1/ctest-core.log` e `build/stage1/ctest-qt.log`.
- `src/emulator.cpp`, `src/main.cpp` e `src/kernel/pthread.cpp` passaram na verificação de sintaxe com as opções do build desktop.
- O build completo `cmake --build build/stage1-desktop --target launcher -j 8` foi tentado, mas não concluiu: o renderizador Vulkan existente falha em atribuições com inicializadores sem tipo em `colorRenderTarget.cpp:212` (`vk::Extent3D`), `textureCache.cpp:1221` (`vk::ClearDepthStencilValue`) e `context.cpp:84` (`vk::Extent2D`). A falha de `context.cpp` foi reproduzida com o cabeçalho original de configuração, sem o novo perfil, em `build/stage1/check-context.cpp.log`. O relatório completo está em `build/stage1/build-launcher-final.log`. Esses arquivos do renderizador não foram modificados nesta etapa. Não há binário completo novo validado nem smoke test da CLI executável.
- As cinco unidades alteradas do launcher Qt também passaram na verificação de sintaxe com Qt 6.11.2 e os cabeçalhos UI/MOC gerados; isso não comprova navegação visual.
- Pré-requisitos de build corrigidos: `<atomic>` estava restrito ao bloco UWP em `sysWindowsVirtual.cpp`, embora usado no desktop; macros de winpthreads colidiam com as declarações `pthread_attr_getguardsize`/`pthread_attr_setguardsize`. A colisão foi reproduzida com o cabeçalho original de configuração, antes de aplicar a proteção nas declarações e o undef da implementação.

Comandos principais (a instalação local do Qt é fornecida por CMAKE_PREFIX_PATH):

```powershell
cmake -S . -B build/stage1-desktop -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
cmake --build build/stage1-desktop --target guest_platform_tests guest_platform_file_tests archive_file_tests launcher_guest_platform_tests -j 8
ctest --test-dir build/stage1-desktop -R '^(guest_platform|guest_platform_files|archive_file|launcher_guest_platform)$' --output-on-failure -j 1
src/uwp/uwp.ps1 configure -BuildDir build/stage1-uwp
```

A configuração UWP foi tentada e parou na ausência de `build/deps/nuget/microsoft.ui.xaml.2.8.7`. Não foi possível compilar ou executar UWP/Xbox nesta entrega; `configure-uwp.log` registra a falha. A primeira tentativa desktop não encontrou Qt no caminho padrão; a instalação foi depois localizada e configurada explicitamente. Não houve instalação de pacotes gerados, uso de jogos proprietários ou mudanças nos projetos de referência.

### Limites e próxima etapa

O estágio entregue é identificado/selecionado, com recusa controlada de execução PS4. Não há comprovação de boot, primeira imagem, menu ou jogabilidade PS4. Testes sintéticos PS5 não substituem regressão com jogos reais. A migração e navegação UWP foram implementadas e revisadas estaticamente, mas precisam de teste no aplicativo. A etapa 2 começa por uma fixture PS4 própria de carregamento, layout de memória, relocations, TLS e entrada/saída observáveis; não basta remover o bloqueio existente no linker.

## Auditoria histórica — etapa 0

Data: 2026-11-07. Escopo desta entrega: investigação estática, mapa dos componentes e plano do primeiro incremento. Nenhum suporte de execução PS4 foi implementado ou validado nesta etapa.

## Estado e procedência

- Projeto alterável: KytyPS5, revisão `0c4c06507116ae3ff2cdb95ffdb4a5351dc1d718`.
- Referências locais: pastas irmãs `sharpemu-main` e `shadPS4-main`, consultadas sem alterações. Ambas sem metadados Git disponíveis; não foi possível determinar revisão ou origem remota. Os hashes ao final identificam arquivos centrais efetivamente consultados.
- A autorização desta entrega é executar a etapa 0 e atualizar a documentação; as instruções de implementação das etapas seguintes permanecem como trabalho futuro.
- As afirmações abaixo descrevem o checkout inspecionado. Aceitação de cabeçalhos, presença de implementação e presença de testes não equivalem a execução comprovada.

## Mapa do KytyPS5 e ciclo de vida PS5

| Área | Arquivo e ponto de integração real | Comportamento observado / impacto PS4 |
|---|---|---|
| Launcher desktop | `src/launcher/src/mainDialog.cpp`, `MainDialog::RunInterpreter`; `src/launcher/src/configurationListWidget.cpp` | O launcher inicia o executável por `QProcess`; a biblioteca lê `sce_sys/param.json`. Acrescentar metadados e perfil preservando esse fluxo desktop. |
| Entrada desktop | `src/main.cpp`, `ParseArgs`; `src/emulator.h`, `RunOptions` | As opções chegam à execução comum; `RunOptions` ainda não contém perfil explícito PS4/PS5. |
| Biblioteca UWP | `src/uwp/src/gameLibrary.cpp`, `ReadGame`, `ScanGames`, `SaveLibrary`; `gameLibrary.h` | Localiza pastas e arquivos `.zar`; extrai nome, ID e versão de `param.json`; persiste em `LocalState/library.json`. Falta leitura PS4 de `param.sfo` e persistência da plataforma. |
| Entrada UWP | `src/uwp/src/emulatorHost.cpp`, criação de `RunOptions` e chamada a `Emulator::Run` | Executa internamente em uma thread; usa `/app0/eboot.bin`. O perfil deve chegar antes da inicialização dos serviços. |
| Configuração | `src/common/emulatorConfig.*`; `src/uwp/src/settings.*` | Opções comuns e UWP precisam transportar seleção automática/manual sem contaminar configurações PS5. |
| Inicialização comum | `src/emulator.cpp`, `Init`, `Run`, `LoadElf`, `Execute` | Carrega `param.json`, inicializa config/log, threads, memória, arquivos, controles, áudio e GPU; monta `/app0` e `/hostapp`, prepara sandbox e conteúdo adicional, registra HLE, carrega ELF e inicia execução. |
| Parsing ELF/SELF | `src/loader/elf.cpp`, `Elf64::IsValid`, `IsNextGen`, validação SELF | Reconhece as assinaturas SELF `4f 15 3d 1d` e `54 14 f5 ee`; aceita ABI 0 ou 2; `IsNextGen()` testa ABI 2. Há parsing parcial reutilizável, sem perfil PS4 completo. |
| Carregamento e módulos | `src/loader/runtimeLinker.cpp`, `LoadProgram`, `LoadProgramToMemory`, `Execute`, `StartModule` | `LoadProgramToMemory` contém `EXIT_NOT_IMPLEMENTED(!is_shared && !is_next_gen)`. O bloqueio atinge imagens não compartilhadas que não são next-gen; não significa rejeição de todo ELF ABI 0. Remover a condição não resolve os contratos restantes. |
| Símbolos e relocations | `src/loader/runtimeLinker.cpp`, `Resolve`, `Relocate`; `src/loader/symbolDatabase.*` | Reutilizar mecanismos após conferir nomes, versões, estruturas e isolamento por perfil. |
| TLS e ABI de entrada | `src/loader/runtimeLinker.h`, `EntryParams`, `ThreadLocalStorage`; `.cpp`, `TlsGetAddr`, `RunEntry` | Conferir layout, índices TLS, argumentos, inicializadores e convenção SysV para PS4 antes de compartilhar. |
| Registro HLE | `src/libs/libs.cpp`, `Libs::InitAll` | Não recebe perfil; registra `InitAgcDriver_1` e VideoOut Gen5. Introduzir composição por plataforma antes do carregamento de imports. |
| Memória e threads | `src/kernel/memory.cpp`, `AllocateProgramMemory`, `HandleCommitFault`, `EnsureGuestCommitted`; `memoryAddressSpace.inc`; `src/kernel/pthread.cpp` | Memória lazy e pager já existem; faixas virtuais, contabilização, proteções e TLS precisam de validação PS4. Não copiar mapas de outra implementação diretamente. |
| Arquivos e arquivos compactados | `src/kernel/fileSystem.*`; `src/common/archive.cpp`; `src/uwp/src/gameSource.*`; `src/common/platform/sysWindowsFileIO.cpp`, `HostCreateFile` | Preservar montagens e ZArchive. UWP usa `CreateFileFromAppW` e chamadas relacionadas, com encaminhamento pelo broker em thread própria. Não introduzir acesso desktop obrigatório. |
| GPU guest | `src/libs/agc.cpp`; `src/graphics/guest_gpu/graphicsRun.cpp`; `command_processor/pm4Handlers.cpp`, `pm4Dispatch.cpp` | Caminho AGC/PM4 atual requer adaptadores de comandos, registradores e descritores GNM. |
| Shaders | `src/graphics/shader/recompiler/ShaderRecompiler.cpp`; `frontend/decode/ShaderDecoder.h`; `src/graphics/shader/programCache.cpp` | Não presumir equivalência de ISA GCN e Gen5. Separar decodificação/semântica PS4 e domínio de cache antes de reutilizar IR e emissão SPIR-V. |
| Backend e apresentação | `CMakeLists.txt`, `KYTY_GPU_BACKEND`; `src/graphics/host_gpu/gpuBackend.h`, `d3d12/`; `src/graphics/presentation/videoOut.cpp` | Um backend por build; Vulkan é padrão e UWP força D3D12. Manter essa seleção de host independente da plataforma guest. |
| Áudio e controles | `src/libs/audio.cpp`, `controller.*`; `src/uwp/src/input.cpp`, `padHost.*` | Reutilizar host após validar estruturas/APIs PS4; a existência do mapeamento PS5 não demonstra ABI PS4 correta. |
| Troca de executável e saída | `src/loader/loadExec.cpp`, `Emulator::LoadExec`; `src/emulator.cpp`, `Execute` | LoadExec por processo só existe nos caminhos desktop habilitados. Execução usa thread destacada, loop gráfico e `quick_exit`; reinicialização interna exige projeto específico. |

Depois de carregar a imagem principal, `RuntimeLinker::Execute` cria a stack guest principal, tenta carregar `/app0/sce_module/libc.prx`, executa `RelocateAll`, aplica cheats quando configurados, inicia libc quando há inicializador e chama `RunEntry` com argumentos na stack guest. O fluxo gráfico roda em paralelo pelo loop de janela; a saída atual usa `quick_exit`, não um contrato geral de reinicialização interna.

A seleção de plataforma precisa anteceder `Init` e `Libs::InitAll`, pois metadados, memória e serviços já são escolhidos antes de `LoadElf`. Identificar apenas dentro do linker seria tarde para essas decisões.

## O que o SharpEmu implementa de fato

Os caminhos desta seção são relativos a `sharpemu-main`.

| Referência | Evidência e utilidade |
|---|---|
| `src/SharpEmu.GnmMode/GnmRuntime.cs`, `CreateLoader`, `UsesCompatibilityMode` | Examina executável em pasta ou `.zar`; seleciona `GnmSelfLoader`; metadados próprios em `param.sfo`. |
| `Loader/GnmLoader.cs`, `IsGnmExecutable`, construtor | A sondagem examina ELF/SELF e OSABI/ABI; a abertura completa valida ELF x86-64 little-endian, tipo, tabelas e segmentos. Sondagem positiva não substitui validação. Rejeita payloads SELF criptografados/comprimidos não suportados. |
| `src/SharpEmu.CLI/Program.cs`, seleção de runtime | Escolhe fábrica GNM quando `CreateLoader` retorna carregador; caso contrário usa a fábrica padrão. A ajuda `--console=ps5` não descreve sozinha essa seleção automática. |
| `src/SharpEmu.GnmMode/GnmRuntime.Factory.cs`, `CreateDefault` | Instancia `SharpEmuRuntime` com `PhysicalVirtualMemory`, `CpuDispatcher`, `GuestGpuMemory` e loader GNM; demonstra composição específica sobre infraestrutura compartilhada. |
| `src/SharpEmu.GnmMode/Loader/GnmSelfLoader.cs`, `LoadCore`, `AllocateImage` | Mapeia segmentos, processa TLS, símbolos, relocations e inicializadores; usa base principal `0x400000` e política própria de alocação. Esses valores são decisões da implementação de referência, não parâmetros automaticamente corretos para o KytyPS5. |
| `src/SharpEmu.GnmMode/GnmExportRegistry.cs`, `CreateExports` | Combina exports gerados Gen4 com overrides para GNM, kernel, saves, vídeo, áudio e outros serviços. O reaproveitamento não é um registro indiscriminado de APIs PS5. |
| `src/SharpEmu.GnmMode/Gpu/GnmCommandPackets.cs`; `Shaders/GcnShaderFrontend.cs` | Adaptadores específicos de comandos e frontend GCN; loader instala esse frontend na infraestrutura AGC compartilhada. Inclui normalização de buffers/imagens e domínio de cache próprio. |
| `src/SharpEmu.CLI/Program.LoadExec.cs`, `TryRunGnmSupervisor`, `RunGnmChild`; `GnmLoadExecSession.cs` | A CLI GNM tem supervisor que inicia processo filho e repete após pedido de LoadExec. A composição do núcleo é integrada, mas o ciclo de vida desktop não satisfaz diretamente a restrição UWP. |
| `tests/SharpEmu.Libs.Tests/Loader/GnmLoaderTests.cs`, `GcnShaderTests.cs`, `GnmLoadExecTests.cs` | Testes sintéticos de ELF/SELF, relocations, TLS, inicializadores, instruções GCN e troca de executável. Foram lidos como referência; não executados nesta entrega. |

## Referências correspondentes no shadPS4

Os caminhos desta seção são relativos a `shadPS4-main`. Foram conferidos no código local; não implicam que o subsistema possa ser copiado integralmente.

| Subsistema | Arquivo / função | Correspondência a investigar no KytyPS5 |
|---|---|---|
| Inicialização e metadados | `src/emulator.cpp`, `Emulator::Run`; `src/core/file_format/psf.h`, `.cpp`, `PSF::Open`, `GetString`, `GetInteger` | Montagem de `/app0`, leitura de `param.sfo`, título, versão e parâmetros PS4 versus `SystemContentLoadParamSfo` que hoje recebe JSON no KytyPS5. |
| Contêiner e ELF | `src/core/loader/elf.cpp`, `Elf::Open`; `src/core/module.cpp`, `Module::LoadModuleToMemory` | Parsing SELF/ELF, segmentos e mapeamento. O shadPS4 distingue bases de executável, módulos de jogo e sistema; isso exige comparação com o alocador do KytyPS5. |
| Linker e TLS | `src/core/linker.cpp`, `LoadModule`, `LoadAndStartModule`, `Relocate`, `Resolve`, `Execute`, `TlsGetAddr`, `AllocateTlsForThread` | Imports/exports, relocations, inicialização, dependências e TLS; comparar com `RuntimeLinker` sem substituir o runtime. |
| Resolução HLE | `src/core/loader/symbols_resolver.*`; `src/core/libraries/gnmdriver/gnmdriver.cpp`; `kernel/file_system.cpp`, `RegisterFileSystem` | Conferir contratos, versões e NIDs antes de registro PS4 no KytyPS5. |
| Memória e kernel | `src/core/memory.cpp`, `.h`; `src/core/libraries/kernel/memory.cpp` | Faixas, granularidade, proteções e erros; preservar lazy commit e swap próprios do port UWP. |
| Sistema de arquivos | `src/core/libraries/kernel/file_system.cpp`, `sceKernelOpen`, `sceKernelRead`; montagens em `Emulator::Run` | Semântica de flags, handles, caminhos e retorno; manter operações de host compatíveis com UWP. |
| GPU | `src/video_core/amdgpu/liverpool.cpp`, `.h`, `pm4_cmds.h`, `pm4_opcodes.h` | Pacotes PM4, estados e submissão PS4 versus processador guest atual. |
| Shaders | `src/shader_recompiler/frontend/translate/translate.cpp`, `Translator::Translate`, `TranslateInstruction` | Semântica GCN, formatos e tradução para IR; não substituir os backends D3D12/Vulkan. |
| Backend gráfico | `src/video_core/renderer_vulkan/vk_rasterizer.cpp` | Referência para comportamento de renderização; não criar dependência Vulkan no UWP. |
| Áudio e input | `src/core/libraries/audio/audioout.cpp`; `src/core/libraries/pad/pad.cpp` | Contratos PS4 a comparar com host de áudio e controle existentes. |

## Matriz de diferenças e dependências

| Área | Base reutilizável | Trabalho específico PS4 | Depende de / aceitação |
|---|---|---|---|
| Identificação | Leitura ELF/SELF e GameSource | Perfil PS4/PS5/desconhecido e evidência diagnóstica | Validação de limites; metadados não autorizam execução por si só. |
| Biblioteca | UI e persistência existentes | `param.sfo`, campo de plataforma, migração de cache | Testes em pasta e `.zar`; entradas antigas continuam carregando. |
| Memória/carregamento | Alocador e linker atuais | Política de endereços, segmentos, relocations e TLS | Fixture PS4 com resultado observável; nenhuma simples remoção do bloqueio. |
| HLE | Resolvedor, arquivos, áudio e input | Registro por perfil e adaptação de estruturas/erros | Validar cada contrato utilizado pelo teste mínimo. |
| GPU | Caches, IR compatível, backend host | GNM, descritores, GCN e layouts PS4 | Testes de comandos/shaders antes de declarar primeira imagem. |
| UWP | Host interno, FromApp, lazy memory e pager | Propagação do perfil e futuro LoadExec viável | Compilar UWP em incrementos compartilhados; hardware separado. |
| Regressões | Testes e execução PS5 existentes | Baseline identificada, cenários e logs comparáveis | Build não prova imagem correta; Series X não comprova Series S. |

## Primeiro conjunto de alterações planejado

Esta é a especificação concreta do incremento seguinte, não uma implementação realizada na etapa 0.

1. Introduzir um pequeno tipo comum de plataforma e resultado de detecção (PS4, PS5, desconhecido; evidências e erro), próximo à camada de carregamento. Conferir convenções de nomes antes de criar arquivos.
2. Implementar sondagem limitada de ELF/SELF usando acesso a arquivos existente, sem inicializar memória guest ou GPU. Validar tamanhos, offsets e overflow; detectar desconhecido, truncado e conflito. ABI 0 isolada não basta para classificar qualquer ELF como PS4.
3. Acrescentar parser limitado de `param.sfo` e adaptar leitura de metadados em `systemContent.*`, bibliotecas desktop/UWP e GameSource; conservar JSON PS5 e arquivos `.zar`.
4. Propagar perfil em `RunOptions`, configuração e entrada comum antes de `Init`; registrar modo detectado e override manual. Override não desativa validações nem força suporte inexistente.
5. Enquanto carregador/runtime PS4 não estiverem validados, retornar diagnóstico de modo reconhecido mas execução não suportada, antes de inicializar o caminho PS5. Não remover o bloqueio atual como atalho.
6. Adicionar testes sintéticos de sondagem e metadados nos padrões dos testes C++ e CMake existentes, sem conteúdo proprietário. Testar também defaults e fluxo PS5.

Critérios: PS4/PS5/desconhecido separados; detecção de entradas inválidas sem crash; evidência conflitante produz diagnóstico em vez de fallback silencioso; modo manual preserva validações; biblioteca e cache compatíveis; nenhuma alegação de boot PS4; compilações desktop e UWP registradas separadamente.

## Incrementos posteriores e critérios

- Carregador: fixture própria mínima com segmentos, relocation e TLS definidos; comprovar endereços esperados e entrada/saída observáveis. Definir antes o mecanismo de término compatível com o runtime; não depender de jogo comercial para o primeiro teste.
- HLE: conjunto mínimo de APIs requeridas pela fixture, tabela de contratos PS4/PS5, falhas explícitas para imports ausentes; testar memória, arquivo e thread.
- GPU: fixture gráfica com resultado verificável, primeiro comandos/descritores, depois shaders GCN; separar caches por semântica de plataforma. Concluir imagem correta no backend exercitado antes de ampliar títulos.
- UWP: acompanhar cada alteração compartilhada com build disponível. Troca de executável terá investigação própria: reinicialização interna ou mecanismo de ciclo de vida permitido; até lá registrar indisponibilidade, sem retorno de sucesso falso.
- Compatibilidade: tabela por título/teste, revisão, plataforma guest/host, backend, estágio, bloqueio e log. Medir primeira imagem separadamente de inicialização quando houver teste de execução.

## Licenças e reutilização

O README do KytyPS5 declara `GPL-2.0-only`, preservando a licença MIT da base Kyty. Os cabeçalhos examinados de SharpEmu (GnmLoader, GnmRuntime e arquivos associados) declaram `GPL-2.0-or-later`; os de shadPS4 (linker e tradutor) também declaram `GPL-2.0-or-later`. Os arquivos LICENSE contêm o texto GPL v2.

Nesta entrega não houve cópia nem adaptação de código de terceiros. Uma adaptação futura deve conferir a licença de cada arquivo e suas dependências, preservar copyright/SPDX e condições aplicáveis, incluindo a opção de GPL v2 nos trechos “or later”; a licença geral não autoriza presumir a licença de todos os componentes. Registrar procedência, arquivo, hash/revisão e alterações de cada trecho adaptado. Estas são constatações dos arquivos locais, não uma auditoria jurídica completa.

## Verificação e limitações desta entrega

- Leituras e buscas com `rg`, `rg --files` e `Get-Content` nos três projetos; conferência das funções e condições citadas. Leitura de README, XBOX_CHANGES, README UWP e seções pertinentes de CMake.
- Nenhum AGENTS.md foi localizado dentro do projeto principal pelas buscas realizadas. O AGENTS.md da referência shadPS4 foi consultado; nenhum arquivo dessa referência foi alterado.
- `git rev-parse HEAD` identifica o KytyPS5. As tentativas de consultar origem e revisão no shadPS4 e a revisão no SharpEmu falharam porque as pastas não são repositórios Git disponíveis.
- Validação documental: conferir existência dos caminhos citados, hashes e `git diff --check` após edição.
- Não foram executados builds, testes de CPU/GPU, jogos, instalação ou hardware Xbox: as alterações desta etapa são documentais. Não há novo resultado funcional PS4 nem comprovação de regressão PS5 nesta entrega.
- Não há orçamento de memória PS4 medido, inventário completo de imports por título ou comprovação de que cada componente existente seja reutilizável. Os contratos concretos serão fechados por teste incremental.

## Identidade dos arquivos de referência

Hashes SHA-256 dos pontos centrais consultados, para permitir comparação futura apesar da ausência de revisão Git:

| Projeto | Arquivo | SHA-256 |
|---|---|---|
| sharpemu-main | `src/SharpEmu.GnmMode/GnmRuntime.Factory.cs` | `a26c4219eea575030039a60985a373efc561c580d0fe252ec2949f2e4b2519ab` |
| sharpemu-main | `src/SharpEmu.GnmMode/Loader/GnmLoader.cs` | `a6ae93f80917fe0efc5714bff1422d6cbb9f5bbbe548fe21f83a5fcf45761993` |
| sharpemu-main | `src/SharpEmu.GnmMode/Loader/GnmSelfLoader.cs` | `2fe77706c2b7a158a8c66288e2bc47a1cf300da4bab467418577b5e38d967ef3` |
| sharpemu-main | `src/SharpEmu.GnmMode/GnmExportRegistry.cs` | `973153bbf048cbc9b15728f9ee686cafbab534a7d226961f0740e9a0683ab363` |
| sharpemu-main | `src/SharpEmu.CLI/Program.LoadExec.cs` | `28d0895c0646d3b9f7fc0659b2a228a71b1ba39adad43401c4256793c8b1dd78` |
| shadPS4-main | `src/core/linker.cpp` | `45ff047dd28e6ef64c88ccb3bedff9d63c65fe54446ece26e2f010ab9bfadcc9` |
| shadPS4-main | `src/core/module.cpp` | `497e7362bd2d5e14df46b6cbcb5dce976b302c00f9aa1d2b661d6863e66f7362` |
| shadPS4-main | `src/core/loader/elf.cpp` | `b0c65f2ee5bdcc5e8e05a2a35f7f92d90392f481748310eb86250c47ca784a4a` |
| shadPS4-main | `src/shader_recompiler/frontend/translate/translate.cpp` | `c7bd57a9e3b226ca8bbf14da29414d5a58d41f741c81a3304aefe02f9c0dc361` |
