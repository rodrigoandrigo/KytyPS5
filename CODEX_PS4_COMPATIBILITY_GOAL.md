# Objetivo para o Codex — Modo de compatibilidade PlayStation 4 no KytyPS5

## Projeto principal

* **Repositório de trabalho (alterar este projeto):** C:\\Users\\rodri\\Dev1\\Projetos\\KytyPS5
* **Referência arquitetural de retrocompatibilidade:** C:\\Users\\rodri\\Dev1\\Projetos\\sharpemu-main
* **Referência técnica de emulação PS4:** C:\\Users\\rodri\\Dev1\\Projetos\\shadPS4-main

## Missão

**Implementar no KytyPS5 um modo nativo e experimental de compatibilidade com jogos de PlayStation 4, inspirado na abordagem de retrocompatibilidade do SharpEmu, aproveitando o código e a infraestrutura existentes do KytyPS5 e consultando o shadPS4 para entender formatos, ABI, bibliotecas, serviços, memória, GPU e demais comportamentos específicos do PS4.**

A meta é permitir que o usuário selecione um jogo PS4 na mesma biblioteca/interface do KytyPS5 e tente executá-lo pelo próprio núcleo do KytyPS5, sem precisar iniciar um executável externo ou adicionar um emulador independente em paralelo.

A implementação deve manter o suporte PS5 existente e respeitar as restrições do port para **Xbox Series X|S / UWP**, especialmente **Direct3D 12**, memória limitada, swap, controles e acesso a arquivos. O suporte Windows/Linux e Vulkan não deve sofrer regressões.

> **Importante:** não presumir que algum componente do PS4 já exista ou funcione. Investigar o código atual antes de escolher arquivos, nomes de classes, APIs ou pontos de integração. A compatibilidade plena com jogos comerciais é um objetivo de longo prazo, não um resultado que possa ser afirmado sem testes reais.

## Diretrizes obrigatórias para o Codex

1. **Trabalhar no repositório principal `Cosmo/KytyPS5`.** As referências `sharpemu` e `shadPS4` servem para pesquisa, comparação técnica e, quando apropriado, adaptação legalmente compatível de ideias/código — não para substituir o núcleo do KytyPS5.
2. **Primeiro investigar, depois implementar.** Identificar no checkout real os subsistemas de carregamento ELF, detecção de jogos, runtime, resolução de símbolos, memória, threads, syscalls, bibliotecas HLE, GPU, shaders, áudio, input, arquivos, configurações, frontend e builds UWP.
3. **Manter PS4 e PS5 explicitamente separados no nível lógico**, mas compartilhar toda infraestrutura que for comprovadamente reutilizável. Não misturar regras de firmware, ABI, bibliotecas, formatos de GPU ou metadados sem discriminação de plataforma.
4. **Priorizar a integração incremental com o núcleo atual.** Não criar um segundo processo/emulador, uma dependência obrigatória do SharpEmu/shadPS4 nem duplicação desnecessária da arquitetura de baixo nível. Decodificadores, adaptadores e contratos específicos de PS4 são permitidos quando as diferenças semânticas os exigirem.
5. **Preservar o port Xbox/UWP.** Qualquer funcionalidade PS4 deve ter caminho de execução viável no ambiente UWP; se algo for inicialmente implementado apenas no desktop, registrar a lacuna, isolar via interfaces e não anunciar suporte Xbox funcional antes de validá-lo.
6. **Evitar regressões.** O modo PS5 permanece padrão para seus títulos; alterações compartilhadas precisam de testes para PS4 e PS5.
7. **Não mascarar progresso.** Diferenciar claramente: identificado, carregado, símbolos resolvidos, inicializado, renderização iniciada, menu, in-game e jogável. Um jogo reconhecido não é um jogo compatível.
8. **Não utilizar nem distribuir conteúdo proprietário**, firmware, chaves, módulos, SDKs, jogos ou arquivos protegidos. Trabalhar com código próprio, documentação pública e arquivos de teste legalmente disponíveis. Respeitar licenças e manter avisos de autoria nos trechos adaptados.
9. **Não fazer refatorações abrangentes sem necessidade.** Alterações pequenas, separadas por subsistema, com motivo técnico, testes e logs de diagnóstico.
10. **Executar o trabalho, não apenas descrever uma proposta.** Implementar cada etapa viável, testar, documentar o resultado e continuar para a próxima, em vez de limitar a entrega a uma análise conceitual.

## Etapa 0 — Auditoria e plano fundamentado no código

* Ler `README.md`, `XBOX_CHANGES.md`, `src/uwp/README.md`, `CMakeLists.txt` e qualquer `AGENTS.md` relevante que exista no checkout.
* Mapear os pontos de entrada do launcher e do emulador; identificar o ciclo de vida completo de um jogo PS5.
* Verificar como a UWP inicia jogos, armazena metadados, usa arquivos externos, mapeia memória, faz swap e seleciona o backend D3D12.
* Localizar a implementação **real**, no SharpEmu, da detecção e seleção de modo PS4, e listar quais diferenças entre PS4 e PS5 ele trata. Não inferir arquitetura apenas pelo README.
* Consultar no shadPS4 as implementações correspondentes a carregamento, links de módulos, APIs HLE, kernel, GPU e sistema de arquivos, associando cada referência a um arquivo/função e revisão quando possível.
* Produzir `docs/PS4_COMPATIBILITY.md` com matriz de diferenças, dependências, estratégia incremental, limitações e critérios de validação.
* Verificar as licenças reais, cabeçalhos e condições de reutilização antes de copiar código. Em caso de dúvida, implementar de forma independente a partir de conceitos e especificações públicas.

**Entregável:** mapa dos componentes e especificação do primeiro conjunto de alterações concreto, priorizado e testável. A implementação funcional começa na etapa 1; a etapa 0 não exige anunciar suporte PS4 nem remover bloqueios do carregador.

## Resultado da etapa 0 — 2026-11-07

Auditoria estática registrada em [docs/PS4_COMPATIBILITY.md](docs/PS4_COMPATIBILITY.md), com mapa dos três projetos, matriz de diferenças, referências de arquivos/funções, licenças, hashes das referências sem Git e critérios do próximo incremento.

Achados confirmados no checkout:

- O KytyPS5 já aceita assinaturas SELF das duas gerações e ABI ELF 0/2. Isso é parsing parcial, não suporte funcional PS4.
- `Elf64::IsNextGen()` verifica ABI 2. `RuntimeLinker::LoadProgramToMemory` interrompe imagens não compartilhadas que não sejam next-gen; não rejeita indistintamente todo ELF ABI 0.
- `Emulator::Run` usa metadados JSON e `Libs::InitAll` registra AGC/VideoOut Gen5 sem receber perfil. A seleção de plataforma precisa ocorrer antes dessas decisões.
- O SharpEmu compõe seu runtime compartilhado com loader, exports e frontend GCN específicos via GnmMode. Sua CLI também usa supervisor/processo filho, incluindo reinícios para LoadExec; apenas a composição do núcleo serve diretamente de referência para execução interna UWP.
- O shadPS4 oferece correspondências concretas para PSF, ELF/SELF, módulos, linker/TLS, HLE, arquivos, PM4 e shaders GCN. Nenhum código das referências foi copiado nesta etapa.

**Estado da entrega:** documentação e plano concluídos; detecção explícita, carregamento e execução PS4 ainda não implementados por esta entrega. Não foram executados builds, jogos ou testes em hardware. O próximo ciclo é a etapa 1, começando por detecção limitada e testável e propagação do perfil, com rejeição controlada da execução ainda indisponível.
## Etapa 1 — Detecção e seleção do modo PS4

Implementar um identificador explícito de plataforma, por exemplo `PS4` / `PS5`, utilizando as convenções já existentes no projeto.

* Detectar títulos PS4 por evidências verificáveis nos metadados e/ou no executável (formato, cabeçalhos, identificadores e outros campos disponíveis); não depender apenas do nome do diretório.
* Aceitar estrutura de arquivos de jogos PS4 já extraídos e legalmente obtidos, conforme o carregador puder suportar. Não prometer suporte a PKG, criptografia ou dumping se não implementado.
* Permitir seleção automática e opção manual de modo PS4 para diagnóstico, sem alterar arbitrariamente a detecção dos jogos PS5.
* Propagar a plataforma escolhida pelo fluxo de biblioteca → configuração → carregamento → runtime → GPU/serviços. Selecionar o perfil antes de inicializar subsistemas, políticas de memória e registro HLE, e não somente dentro do linker.
* Exibir a plataforma PS4/PS5 na biblioteca e em logs diagnósticos; manter navegação por controle no Xbox.
* Tratar formatos desconhecidos, entradas truncadas e evidências conflitantes com diagnóstico explícito; definir a precedência das evidências e registrar overrides. A seleção manual não desativa validações de formato nem autoriza execução ainda não suportada.
* Incluir metadados PS4 de `sce_sys/param.sfo` nas bibliotecas desktop/UWP e em `.zar`, preservando JSON PS5 e compatibilidade do cache existente.
* Rejeitar entradas não suportadas com mensagem específica e útil, evitando crash.

**Critério de aceitação:** jogos e executáveis sintéticos PS4/PS5 e formatos desconhecidos são classificados separadamente e têm o perfil propagado corretamente. Enquanto carregador/runtime PS4 não estiverem validados, a execução PS4 deve ser recusada de forma controlada antes de inicializar o caminho PS5. Exigir testes de detecção, conflitos, override e regressões PS5; alcançar o entry point pertence à etapa 2.

## Resultado da etapa 1 — 2026-11-07

Detecção ELF/SELF e metadados PS4/PS5, perfil solicitado/selecionado, opção manual, indicação nas bibliotecas desktop/UWP e recusa antecipada de execução PS4 implementados. O fluxo PS4 ainda não inicia o runtime; carregar e executar pertencem à etapa 2. O perfil PS5 selecionado é disponibilizado na configuração comum aos serviços e à GPU.

Detalhes, arquivos e limitações em [docs/PS4_COMPATIBILITY.md](docs/PS4_COMPATIBILITY.md). Passaram os testes de sondagem/SFO, inspeção em pasta e `.zar`, regressão de arquivos compactados e persistência da seleção no launcher. A configuração UWP foi tentada, mas a dependência WinUI 2 não está disponível; build e execução UWP/Xbox permanecem sem validação. A seleção UWP é diagnóstica global, padrão automática; opções específicas por título continuam previstas na etapa 5. O build completo desktop foi tentado, mas permanece bloqueado por atribuições ambíguas de estruturas Vulkan no código existente; os arquivos de entrada alterados passaram na verificação de sintaxe. Portanto, a etapa tem implementação e testes focados aprovados, com validação integral de build/runtime pendente.

## Etapa 2 — Carregador e execução de módulos PS4

* Comparar os formatos de ELF/SELF, layout de segmentos, relocations, TLS, imports/exports, hashes/NIDs, dependências de módulos e inicialização com os usados pelo KytyPS5.
* Adaptar o carregador existente para o subconjunto PS4 necessário, mantendo parsing validado, limites de tamanho e erros explícitos para formatos não suportados. Conferir bases de executável/módulos, granularidade, TLS e inicializadores com fixture própria. Não remover isoladamente `EXIT_NOT_IMPLEMENTED(!is_shared && !is_next_gen)` como prova de suporte.
* Introduzir resolução de símbolos e ligação de módulos específicas de PS4, reutilizando abstrações existentes. Evitar símbolos PS4 tratados como PS5 por engano.
* Revisar ABI AMD64, convenção de chamadas, TLS, exceções, threads, inicialização e encerramento de processos em função da diferença de ambiente.
* Não assumir que nomes, hashes, assinaturas ou versões das bibliotecas PS4 sejam intercambiáveis com PS5.

**Critério de aceitação:** um executável de teste PS4 suportado é carregado, tem os imports de teste resolvidos e alcança seu ponto de entrada com rastreabilidade por logs.

## Resultado da etapa 2 — 2026-11-07

Implementado o adaptador de carregamento PS4 `PreparePs4Module` e sua entrada `RuntimeLinker::PreparePs4Program`, com registro separado de símbolos PS4, ELF/SELF limitados, segmentos/BSS, imports com versões, relocations, template TLS e listas init/fini. A fixture própria foi materializada e executada nativamente no desktop: ELF e SELF chegaram ao entry point em `0x400100`, chamaram o import resolvido e retornaram 42. Cinco testes focados passaram.

**Limite da entrega:** o teste nativo usa o adaptador e os helpers existentes de memória; não passa pelo ciclo completo `RuntimeLinker::Execute`. A API de preparação não registra um `Program` nem inicia HLE, threads ou GPU. O lançamento de jogos PS4 continua recusado. A integração do perfil ao runtime, TLS de threads, ligação de dependências guest e ABI completa permanecem pendentes; não há comprovação de boot comercial ou Xbox.

WinUI/WebView2 foram localizados em `teste1` e aceitos na verificação de configuração UWP. O bloqueio seguinte é a distribuição/caminho `spirv_to_dxil`. Nenhuma instalação ou deploy foi realizado. Detalhes e evidências em [docs/PS4_COMPATIBILITY.md](docs/PS4_COMPATIBILITY.md).

## Etapa 3 — Camada de compatibilidade do sistema PS4 (HLE)

Criar um perfil de runtime PS4 que reutilize a infraestrutura existente, adicionando somente as diferenças necessárias, por prioridade de bloqueio de boot:

* APIs essenciais de kernel, memória virtual, sincronização, threads, temporização e eventos.
* File I/O, caminhos virtuais, diretórios, handles, permissões, save data e sandbox de escrita.
* Carregamento de módulos e bibliotecas HLE PS4, com tratamento de versões e símbolos não implementados.
* Input de controles, configuração de usuários, vídeo/áudio e outras APIs somente quando utilizadas por testes concretos.
* Contratos claros para retornos, códigos de erro, estruturas, alinhamentos, tamanhos e chamadas assíncronas.
* Logs úteis para APIs ausentes sem inundar o log nem retornar sucesso falso onde o jogo espera efeitos reais.

**Critério de aceitação:** um teste PS4 consegue inicializar runtime, exercitar operações básicas de memória, arquivo, thread e finalizar sem falhas inesperadas.

### Entrega da etapa 3 — 2026-11-07

Perfil CPU/HLE PS4 conectado ao `RuntimeLinker`, memória, filesystem e pthreads existentes, com registro PS4 separado e 15 exports de `libkernel`. A opção explícita `--ps4-runtime` ativa o perfil diagnóstico; o lançamento normal/UI continua recusando PS4 enquanto o caminho gráfico não estiver implementado.

A fixture própria ELF/SELF chama os exports em código AMD64 e exercita memória RW, leitura de `/app0`, gravação em `/data`, mutex, thread, TLS por thread, init/fini e unload. Há um caso que deixa recursos para a limpeza automática, além de rejeições de imports ausentes e RWX. Ponteiros/handles e caminhos são verificados; operações fora do subconjunto produzem erro.

**Validação:** critério de aceitação atendido no Windows desktop x64. Oito testes aprovados (incluindo runtime PS4, caminho `Emulator::Run`, carregador, detecção, arquivos, biblioteca e sampler de áudio). ELF, SELF e limpeza automática completaram com `status=0x600dcafe`, uma leitura, uma escrita, uma thread iniciada/unida e três consultas TLS por execução. Log: `build/stage3/ctest-final.log`. Foram corrigidos também os bloqueios de integração do build necessários para compilar o teste completo. O executável normal/UI e gameplay PS5 não foram exercitados; a confirmação é do perfil CPU/HLE descrito nesta etapa.

**Integração UWP:** `spirv_to_dxil` foi compilado a partir de `C:/Users/rodri/Dev1/Projetos/Mesa-UWP`, em cópia separada com as extensões do Kyty. Distribuição: `build/stage3/spirv_to_dxil-dist`; origem e hashes em `manifest.json`. Cinco shaders DXIL passaram no validador do SDK, incluindo geometria; acessos além dos 36 bytes enviados pelo backend são recusados no modo compacto. A configuração e o build completo UWP/D3D12 passaram com WinUI/WebView2 existentes em `teste1`. Log: `build/stage3/build-uwp-final.log`. Isso inclui a compilação do perfil CPU/HLE PS4, sem execução dele em UWP.

**Limites:** isso não habilita boot comercial ou GPU PS4. Eventos, saves por título, PRX/dependências dinâmicas, TLS externo, input/áudio e LoadExec continuam pendentes de testes concretos. O perfil tem limites deliberados e usa `_Ps4Data/diagnostic`; não é contrato completo do sistema PS4. Nenhuma instalação/deploy ou execução AppContainer/Xbox foi realizada. Contratos e evidências em [docs/PS4_COMPATIBILITY.md](docs/PS4_COMPATIBILITY.md).


## Etapa 4 — GPU PS4 e renderização

* Investigar diferenças entre o caminho gráfico PS5 atual e o PS4 (comandos, formatos, registradores, estados de pipeline, descriptors, recursos e sincronização).
* Consultar o frontend GPU do shadPS4 como referência técnica, mas preservar os backends de host já existentes no KytyPS5.
* Integrar a tradução PS4 a uma representação compartilhada apenas quando semanticamente compatível; criar decodificadores e conversões separados quando necessário.
* Identificar diferenças de instruções ISA, shaders e formatos de GPU PS4 frente ao pipeline PS5; não reutilizar o recompiler PS5 sem validação.
* **No Xbox/UWP, utilizar o backend Direct3D 12 existente**, incluindo a infraestrutura de conversão de shaders quando aplicável, sem introduzir dependência de Vulkan em tempo de execução.
* No desktop, preservar o backend Vulkan; evitar ifdefs de plataforma espalhados pelo código compartilhado.
* Registrar limitações gráficas conhecidas, inclusive comandos não implementados ou recursos indisponíveis no Xbox.

**Critério de aceitação:** teste gráfico PS4 reproduz um resultado verificável, sem comprometer testes do renderizador PS5; o caminho UWP/D3D12 compila e é exercitado quando o hardware estiver disponível.

### Incremento da etapa 4 — 2026-11-07

Implementado um frontend PS4 separado para transferências PM4: NOP opaco, `DMA_DATA` com preenchimento imediato em memória e `WRITE_DATA` incremental. O lote é validado por inteiro antes de produzir um plano independente de backend; controles, comandos ou endereços fora do subconjunto são recusados. Nenhum shader GCN é encaminhado ao decodificador RDNA2 PS5.

O diagnóstico executa as transferências em buffers privados, copia o resultado para uma imagem RGBA8 e confere todos os pixels após leitura de volta e conclusão da GPU. Passou na Radeon RX 6600M com D3D12 e Vulkan e também em D3D12 WARP: 64×32, 65×17 com padding e 1×1. O D3D12 passou com a camada de depuração e zero erros. As imagens de 64×32 dos dois backends são idênticas byte a byte. O código D3D12 integra o diagnóstico de shaders existente; o build completo UWP e a conferência de imports passaram, sem instalação.

**Shaders e draw:** frontend GCN próprio ligado à IR compartilhada, com VS/PS de um subconjunto de ALU e exports, ABI VS `v0` e recusa explícita do que não é suportado. Quatro módulos SPIR-V e três pares de shaders DXIL passaram nos validadores. O diagnóstico rasteriza um triângulo real, com testes de scissor, blend e máscara de canais em D3D12 hardware/WARP e Vulkan. Os nove readbacks passaram, com imagens iguais byte a byte e zero erros nas camadas de validação. Dezessete testes CTest, incluindo a suíte PS5 `shader_cfg`, passaram; build UWP/D3D12 e imports aprovados. Evidências em [docs/PS4_GCN_RASTER.md](docs/PS4_GCN_RASTER.md).

**Estado:** transferências e draw GCN diagnóstico validados; etapa 4 geral ainda parcial. Não há shaders arbitrários, descriptors/tiling PS4 completos, integração `sceGnmSubmitCommandBuffers`/video-out com o runtime PS4 ou execução Xbox. Os alvos e o estado de rasterização são fornecidos explicitamente ao diagnóstico; não representam descriptors/registradores GCN completos decodificados. A recusa de lançamento PS4 normal permanece. Nada foi instalado. Auditoria e limites em [docs/PS4_GPU.md](docs/PS4_GPU.md).

## Etapa 5 — Integração com Xbox/UWP

* Integrar a plataforma PS4 à biblioteca, tela de opções, inicialização e relatório de erros existentes.
* Reutilizar gamepad Xbox, gerenciamento de memória sob demanda, swap e cache, observando diferenças de layout/uso de memória dos jogos PS4.
* Garantir que nenhum recurso novo dependa obrigatoriamente de processos filhos, permissões, APIs desktop ou caminhos de arquivo incompatíveis com UWP.
* Tratar `LoadExec` e reinício como contratos específicos: o supervisor GNM do SharpEmu usa processos filhos, abordagem que não pode ser transportada diretamente para UWP. Até existir solução validada, registrar indisponibilidade com erro explícito.
* Acompanhar alterações compartilhadas com compilação UWP disponível desde os primeiros incrementos; registrar testes Series X e Series S separadamente.
* Respeitar acesso por USB e os mecanismos existentes de leitura, salvamento e configuração.
* Permitir opções por jogo sem contaminar configurações de títulos PS5.
* Manter o projeto compilável para Xbox Series X|S e para os alvos desktop suportados.

**Critério de aceitação:** o modo PS4 aparece corretamente no aplicativo UWP; carregar um título PS4 suportado segue o caminho interno previsto, sem depender de um programa externo. Execução real no Xbox só pode ser declarada após teste no dispositivo.

## Etapa 6 — Testes, compatibilidade e regressões

Implementar testes automatizados com executáveis/dados de teste redistribuíveis e sem dependências proprietárias. Incluir:

* Identificação PS4 versus PS5 e formatos desconhecidos.
* Parsing robusto e entradas corrompidas/truncadas.
* Símbolos, importações, TLS, relocations e ciclo de vida dos módulos.
* HLE PS4: memória, threads, arquivos e erros esperados.
* Tradução gráfica: testes de comando/shader/pipeline, conforme viabilidade.
* Regressões de carregamento, memória e GPU PS5.
* Build e testes nos alvos que o ambiente disponibilizar; registrar explicitamente os que não puderam ser executados, especialmente UWP em hardware Xbox.

Documentar uma tabela por título/teste com plataforma, revisão, ambiente, backend, estágio alcançado, erro bloqueante e evidência (logs). Não usar 'jogável' sem validação suficiente.

## Ordem de execução e prioridades

1. Auditar repositórios e produzir o mapa técnico.
2. Implementar detecção/seleção PS4 + testes.
3. Implementar o primeiro fluxo funcional de carregamento PS4 + testes.
4. Resolver runtime/HLE mínimo necessário para esse fluxo.
5. Habilitar caminho gráfico PS4 com backend existente e testes focados.
6. Integrar todos os componentes à UWP e otimizar conforme evidências.
7. Expandir compatibilidade jogo a jogo, corrigindo falhas específicas.

Não aguardar suporte completo de GPU ou HLE para entregar os primeiros resultados testáveis. Cada etapa deve manter o projeto em estado compilável sempre que possível.

## Definição de pronto (por entrega)

Uma alteração só deve ser considerada concluída quando:

* [ ] Está implementada no código do KytyPS5, não somente documentada.
* [ ] Possui motivação e referência aos arquivos/funções alterados.
* [ ] Inclui testes pertinentes ou explicação concreta da impossibilidade.
* [ ] Passa pelo build/testes disponíveis, ou explicita comandos, erros e limitações reais.
* [ ] Não quebra o caminho PS5 já existente.
* [ ] Considera o impacto em UWP/D3D12 e desktop/Vulkan.
* [ ] Atualiza `docs/PS4_COMPATIBILITY.md` e lista limitações conhecidas.
* [ ] Não contém conteúdo proprietário incluído indevidamente e cumpre licenças aplicáveis.

## Formato obrigatório do relatório do Codex ao final de cada ciclo

1. **Análise do estado atual:** o que existia e o que faltava, com caminhos de arquivo concretos.
2. **Alterações realizadas:** arquivos modificados e comportamento implementado.
3. **Referências consultadas:** correspondências técnicas com SharpEmu e shadPS4; distinguir inspiração de código adaptado.
4. **Verificação:** comandos executados, resultados de build/testes e logs relevantes.
5. **Compatibilidade:** estágio comprovado de PS4; status das regressões PS5; status UWP/Xbox.
6. **Limitações:** bloqueios reais, dependências e recursos não implementados.
7. **Próxima tarefa concreta:** menor melhoria testável que avance boot/compatibilidade.

## Primeira instrução operacional ao Codex

> Examine o checkout atual de `Cosmo/KytyPS5` e os pontos correspondentes nos repositórios `sharpemu/sharpemu` e `shadps4-emu/shadPS4`. Identifique como introduzir um perfil de execução PS4 dentro do núcleo KytyPS5 sem prejudicar PS5 nem o port Xbox/UWP. Crie `docs/PS4_COMPATIBILITY.md` com a auditoria e implemente imediatamente o primeiro incremento seguro e testável: detecção explícita PS4/PS5, seleção de perfil na inicialização e testes automatizados associados. Se o checkout revelar pré-requisitos, resolva primeiro o menor bloqueio necessário e documente a decisão. Execute os testes possíveis, apresente os resultados reais e prossiga incrementalmente com carregador PS4, HLE e GPU.

---

**Resultado final desejado:** uma única aplicação KytyPS5 capaz de identificar e executar progressivamente jogos PS5 e PS4, selecionando o perfil de compatibilidade correto e usando a infraestrutura de renderização, memória, controles e interface existente — incluindo o backend Direct3D 12 no Xbox Series X|S/UWP.
