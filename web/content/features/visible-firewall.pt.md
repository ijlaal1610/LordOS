## Em poucas palavras

A maioria dos firewalls pensa em portas e endereços. O firewall do LordOS pensa em **apps**: você decide quais dos seus apps podem usar a rede, e quanto dela. E, como ele está integrado ao Monitor, você vê o que cada app está fazendo antes de decidir.

[[shot:0]]

Imagine um prédio com um porteiro na porta de cada apartamento, em vez de um só na entrada da rua. Quando um app tenta abrir uma conexão, o porteiro da porta dele confere a lista numa fração de microssegundo e deixa passar ou responde "não permitido". O porteiro fica dentro do kernel Linux, então um app insistente não consegue simplesmente dar a volta nele.

[[diagram:path]]

## O que você vê e decide

- **Monitor → Rede** lista os apps com conexões, quanto cada um enviou e recebeu, e um mapa-múndi com um arco até cada país com que eles falam. Cada app tem um botão de **Regra**: desligue e o app perde a rede na hora, com contadores de conexões permitidas e negadas vindos direto do kernel.
- **Ajustes → Privacidade → Rede dos apps** tem um modo e duas listas:
  - **Silencioso** (o padrão): o que não tem regra é permitido e fica anotado, para você revisar no Monitor. **Desligado**: só valem as suas negações explícitas e as restrições.
  - **Apps confiáveis**: permitidos sem anotação. Uma negação explícita que você definiu continua valendo.
  - **Apps restritos**: marque o que um app não pode fazer. As restrições de rede são **Sem internet** (só LAN), **Sem LAN**, **Sem rede** nenhuma e **Sem conexões de entrada**. As outras tiram dispositivos e dados (veja [Além da rede](#alem-da-rede)).

[[shot:1]]

[[shot:2]]

## Como funciona

**1. Todo app tem um nome que o kernel enxerga.** Quando você abre um app Linux pelo lançador de apps, o `lord-server` pede ao root que o coloque num cgroup próprio, `app-lordos-<id>.scope`. Os apps `.lpk` instalados ganham `app-lordos-lpk-<id>.scope`. Esses escopos são criados pelo root e pertencem a ele, então um app não consegue se mudar para outro.

**2. Um pequeno serviço root aplica as regras.** O `lord-netperm` carrega seus programas eBPF no cgroup do serviço do LordOS, que contém todas as sessões:

| Gancho do kernel | O que confere |
|---|---|
| `connect4`, `connect6` | TCP e UDP conectado |
| `sendmsg4`, `sendmsg6` | UDP enviado com endereço |
| `bind4`, `bind6` | entrada: "sem conexões de entrada" e regras de entrada |
| `sock_create` | "sem rede": o app nem consegue criar um socket de internet |
| `cgroup/dev` | os dispositivos de câmera, USB, captura ALSA e framebuffer de um app Linux, no `open()` |
| LSM `socket_create` | os sockets Bluetooth de um app Linux |

Cada programa descobre a qual app pertence o processo que chamou (pelo ancestral do cgroup no nível do escopo, então processos filhos contam como o mesmo app), decide e devolve permitir ou negar. Um app negado recebe `EPERM`, "Operation not permitted".

[[shot:4]]

**3. Suas regras viram mapas do kernel.** Você muda as regras na sessão; o `lord-server` as salva em `~/.config/lordos/firewall.json` e manda ao `lord-netperm` uma única mensagem sem dados: `RELOAD`. O serviço então lê o arquivo por conta própria, com validação estrita, e compila as regras em mapas hash e de correspondência pelo prefixo mais longo. As regras nunca trafegam pelo socket.

**4. Nada roda por pacote.** As verificações acontecem quando uma conexão é aberta ou uma porta é vinculada, não a cada pacote. Medido numa VM de teste: cerca de **0,5 µs** por connect UDP, TCP dentro do ruído e nenhuma mudança na vazão (`docs/FIREWALL.md`).

## Funciona por domínio?

O motor funciona, com cuidado sobre a origem dos nomes. Uma conexão só carrega um endereço IP, então, para aplicar uma regra como "permitir só `example.com`", o firewall precisa saber a quais nomes um endereço pertence. Ele aprende isso **só com o resolvedor do sistema**: assina o monitor do systemd-resolved (ou, sem ele, lê as respostas da porta 53 dos servidores em `/etc/resolv.conf`). Ele nunca confia em respostas DNS que um app recebeu nos próprios sockets, que um app malicioso poderia forjar.

[[diagram:dns]]

Na VM de teste, "negar tudo exceto example.com" deixou passar `https://example.com` e negou `https://www.debian.org`, 20 de 20 vezes, mesmo com o cache do resolvedor vazio. Um app que usa seu próprio DNS sobre HTTPS obtém endereços que o firewall nunca aprendeu, então, sob uma regra dessas, eles são negados.

**Onde escrevê-las.** Ajustes → Privacidade → **Regras de rede por app** mostra o padrão de cada app ("Permitir tudo" ou "Bloquear tudo exceto as regras") e suas regras na ordem em que vencem, já que vence a mais específica, com quando cada uma coincidiu pela última vez. A folha que adiciona uma regra aceita um domínio exato, `*.domínio`, um endereço ou uma rede, com porta e protocolo opcionais, e confere o que você digita enquanto digita. Em **Monitor → Rede**, "Permitir este domínio" ou "Bloquear este domínio" num destino de um app abre a mesma folha, preenchida com o nome que o resolvedor do sistema deu para aquele endereço.

**A pegadinha, dita com honestidade: DNS criptografado.** Um navegador que resolve nomes por conta própria com DNS sobre HTTPS nunca pergunta ao resolvedor do sistema, então os nomes dele não são aprendidos. "Permitir só example.com" continua valendo (os endereços que ele obtém nunca foram aprendidos, então são bloqueados), mas "Bloquear example.com" não o impede. As regras por endereço e por rede, o padrão do app e suas restrições continuam valendo, e os Ajustes dizem isso abaixo das regras.

## Além da rede

A mesma lista de apps restritos também pode tirar a câmera, o microfone, a tela, o USB, os seus arquivos, a área de transferência, o Bluetooth e as notificações. Uma restrição vence as permissões que você deu antes: o app é recusado sem aviso, e restringir um app em execução tira dele a câmera e o microfone na hora. Cada uma é aplicada onde o recurso é entregue:

[[diagram:restrictions]]

- **Os apps isolados do LordOS** (apps `.lpk` e Senhas): as oito, no broker que já intermedeia tudo o que eles recebem.
- **Os apps incluídos** (a linha "LordOS"): câmera, microfone, tela e notificações. Essa linha é o próprio desktop, então arquivos, USB, área de transferência e Bluetooth são restringidos por app.
- **Os apps Linux abertos pelo lançador de apps:** os dispositivos de câmera, USB, captura ALSA e framebuffer são recusados no kernel (`cgroup/dev`); os sockets Bluetooth, por um programa BPF LSM; o microfone e as câmeras do PipeWire, pelo guarda do PipeWire do LordOS, ao vivo, identificando cada cliente pelas credenciais do seu socket; as notificações, pelo servidor de notificações do LordOS, que pergunta ao barramento quem está chamando e descarta a notificação se não conseguir saber; e "sem acesso aos seus arquivos" abre o app, a partir da próxima abertura, em namespaces próprios com uma pasta pessoal vazia e Landlock. A captura de tela nunca esteve ao alcance deles: não há protocolo de captura nem servidor X.

## As perguntas do leitor cético

**Vale também para os apps Linux?** Sim. Esse é um dos pontos principais: Chromium, Firefox ou qualquer app aberto pelo lançador de apps tem seu próprio escopo e suas próprias regras. Os apps incluídos no LordOS e os programas iniciados pelo Terminal contam juntos como "LordOS".

**Um app consegue escapar do seu escopo?** Isso foi testado. Um app Linux não consegue se inscrever em outro cgroup, porque todo arquivo `cgroup.procs` da árvore da sessão pertence ao root. Ele ainda pode pedir ao gerenciador systemd do usuário que inicie um novo processo fora da árvore (`systemd-run --user`). O LordOS fecha isso também: o `lord-netperm` anexa seus programas também ao `user@<uid>.service` de cada usuário, e qualquer processo ali fica sujeito à união mais estrita das restrições dos apps restritos daquele usuário que estão rodando. Testado com `systemd-run --user --scope`, um serviço transitório e `setsid`: todos negados.

[[shot:3]]

**Posso permitir uma conexão só uma vez, ou ser perguntado?** O motor de regras conhece três durações (para sempre, até o app fechar, só uma vez) e um modo "perguntar", mas o aviso de pergunta ainda não foi construído: hoje "perguntar" se comporta como silencioso. Ele foi projetado como um aviso de nova tentativa: a primeira conexão é negada, você recebe *Permitir uma vez / Sempre / Negar*, e a nova tentativa do app passa.

**O que acontece se o serviço parar?** Os vínculos dele são fechados e tudo fica permitido. O desktop não depende dele. Na ISO final, o serviço estava rodando com seus sete programas de rede anexados.

**O próprio serviço é perigoso?** Ele é mantido pequeno de propósito: não executa programas, não aceita argumentos, não tem sockets de internet próprios (sua unit só permite sockets Unix e de pacote) e aceita um único tipo de mensagem. Ele usa um carregador próprio sobre a chamada de sistema `bpf()`, com os programas compilados dentro do binário; sem libbpf nem bcc.

## Limites honestos

- **ICMP** (ping) só é bloqueado por "sem rede". **Conexões de entrada numa porta que já está aberta**, e `listen` sem `bind`, não são conferidas: isso exigiria um gancho por pacote.
- **A união do `user@` é grosseira:** enquanto um app restrito roda, todo processo no gerenciador systemd daquele usuário fica sujeito à união das restrições.
- **Um escopo recém-criado** é registrado em microssegundos, mas uma conexão feita antes disso passaria pelas regras padrão.
- **O Monitor lista os destinos como IPs e países;** o nome que o resolvedor do sistema deu aparece quando você abre "Permitir / Bloquear este domínio". O Monitor nunca faz DNS reverso, então nenhum endereço sai da máquina só porque você olhou para ele.
- **As regras por domínio não veem apps que usam o próprio DNS criptografado** (veja acima).
- **O que as restrições não cobrem para apps Linux:** o Bluetooth através do BlueZ no barramento do sistema (só os sockets Bluetooth diretos são recusados), o barramento do sistema em geral, a área de transferência e programas iniciados fora do lançador de apps. Um cliente novo de PipeWire de um app restrito tem alguns milissegundos antes de o guarda restringi-lo, e enquanto roda um app com a câmera ou o Bluetooth restritos, os seus próprios serviços de áudio também não conseguem abrir uma câmera nem um socket Bluetooth novos.
- **O arquivo de regras pode ser editado por apps Linux sem sandbox**, já que eles rodam como você, a menos que estejam restritos com "sem acesso aos seus arquivos". Os apps `.lpk` instalados não o alcançam.

## Leia o código

- [server/src/netperm/np.bpf.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/np.bpf.c): os programas eBPF de cgroup e LSM
- [server/src/netperm/netperm.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/netperm.c): o serviço root
- [server/src/netperm/npcompile.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npcompile.c): regras em mapas do kernel
- [server/src/netperm/npdns.c](https://github.com/lordbasex/LordOS/blob/main/server/src/netperm/npdns.c): nomes vindos do resolvedor do sistema
- [server/src/fw/rules.c](https://github.com/lordbasex/LordOS/blob/main/server/src/fw/rules.c): regras, precedência e o leitor estrito do arquivo
- [server/src/ui/js_fw.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/js_fw.c): `lord.fw`, a API da sessão
- [apps/monitor/Firewall.jsx](https://github.com/lordbasex/LordOS/blob/main/apps/monitor/Firewall.jsx) e [apps/monitor/FwRules.jsx](https://github.com/lordbasex/LordOS/blob/main/apps/monitor/FwRules.jsx): as seções de Ajustes
- [server/src/sec/restrict.c](https://github.com/lordbasex/LordOS/blob/main/server/src/sec/restrict.c) e [server/src/sec/pwguard.c](https://github.com/lordbasex/LordOS/blob/main/server/src/sec/pwguard.c): as restrições no broker e o guarda do PipeWire
- [server/tests/netperm_integration.sh](https://github.com/lordbasex/LordOS/blob/main/server/tests/netperm_integration.sh): o teste com o kernel, 18 de 18
- [docs/FIREWALL.md](https://github.com/lordbasex/LordOS/blob/main/docs/FIREWALL.md) e [docs/MONITOR.md](https://github.com/lordbasex/LordOS/blob/main/docs/MONITOR.md)
