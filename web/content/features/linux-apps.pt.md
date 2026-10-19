## Em poucas palavras

O LordOS tem um desktop próprio, mas por baixo continua sendo Debian. As dezenas de milhares de programas do repositório do Debian estão disponíveis, e os apps gráficos feitos com GTK ou Qt, além de navegadores como Chromium e Firefox, abrem como janelas do LordOS, com a mesma moldura, o mesmo Dock e o mesmo modo claro ou escuro dos apps nativos.

[[shot:0]]

O segredo é uma língua em comum. Os apps Linux falam **Wayland**, um protocolo para dizer "esta é a minha janela, estes são os meus pixels, me mande o mouse e o teclado". O servidor gráfico do LordOS também fala essa língua. Então um app que não sabe nada do LordOS ainda consegue viver no desktop dele, como um viajante que fala o idioma local e não precisa de guia.

[[diagram:linux]]

## Como instalar

- **A Loja** mostra os apps do Debian por categoria, com busca, e mostra um plano (o que será instalado ou removido) antes de fazer qualquer coisa. Ela também instala os apps `.lpk` do próprio LordOS e arquivos `.deb` locais.
- **O motor de pacotes próprio do LordOS** lê suas fontes do apt, verifica os índices assinados com código OpenPGP próprio, resolve as dependências e confere cada arquivo baixado contra o SHA-256 do índice assinado. Só então o `dpkg`, a ferramenta do próprio Debian, desempacota e configura o pacote. O banco de dados do dpkg continua sendo escrito só pelo dpkg, então `apt` e `dpkg` seguem funcionando normalmente no Terminal.
- **O Terminal** funciona como em qualquer sistema Debian. Cada aba já vem com as variáveis do Wayland definidas, então iniciar um programa GTK ou Qt por ele simplesmente funciona.

[[shot:1]]

## Como abrir

Os apps com um arquivo `.desktop` aparecem no lançador de apps, no menu iniciar do tema Clássico e na Busca, com seus ícones. Clicar num deles passa por uma porta propositalmente estreita no `lord-server`:

1. A interface só pode passar o **id** de um arquivo `.desktop` já instalado em `/usr/share/applications` ou `~/.local/share/applications`, nunca um comando.
2. O `lord-server` relê esse arquivo, confere que é um aplicativo de verdade, visível e que não roda em terminal, e monta o comando a partir da linha `Exec` seguindo a especificação, sem shell e sem argumentos de quem chamou. O "Abrir com" do Arquivos pode acrescentar exatamente um arquivo, só se o app declarar os tipos de arquivo que trata e o arquivo estiver na sua pasta pessoal ou num disco que você montou.
3. O app é iniciado no seu próprio cgroup, `app-lordos-<id>.scope`, criado pelo root, para que o Monitor, o firewall por app e suas restrições saibam a qual app pertence cada processo. Se você restringiu os seus arquivos para o app, é aqui também que ele recebe a própria visão: namespaces próprios, uma pasta pessoal vazia só com as pastas dele, e Landlock.

O JavaScript do LordOS não consegue escrever arquivos `.desktop`, então a interface não tem como inventar um comando: ela só pode escolher um app que já está instalado.

## Como eles ficam e se comportam

- **A moldura.** Os apps que pedem ao compositor para desenhar sua moldura (apps Qt, foot, Chromium com a barra de título do sistema) recebem a moldura do tema. Os apps GTK 3, como o Firefox, a recebem por um segundo protocolo de decoração que o LordOS também fala. Os apps GTK 4 que desenham a própria barra a mantêm, e o LordOS apara a sombra deles.
- **Claro e escuro, ao vivo.** O próprio LordOS serve o portal de desktop "Settings" no barramento da sessão, então os apps GTK 4 e libadwaita seguem o modo, a cor de destaque e a ordem dos botões. Os apps GTK 3 seguem pelo keyfile do GSettings, e o LordOS traz um tema GTK combinando. Os dois mudam ao vivo quando você troca de modo.
- **Uma só área de transferência.** Um texto copiado num app Linux cola no LordOS e vice-versa; as imagens continuam disponíveis depois que o app de origem fecha e aparecem no histórico da área de transferência (❖V).
- **Nítido em qualquer escala.** Cada tela é um `wl_output` separado, e a escala fracionária deixa GTK 4, Qt 6 e Chromium renderizarem com nitidez de 125 a 175 %.
- **Gestos do touchpad.** O driver de touchpad próprio do LordOS repassa os gestos de pinça, deslizar e segurar aos apps Linux, então o zoom com pinça funciona nos navegadores.
- **Jogos e apps de GPU.** Com uma GPU, os apps podem entregar buffers de GPU direto ao LordOS (`linux-dmabuf`), usar tela cheia de verdade, prender o ponteiro e receber o movimento bruto do mouse.
- **As notificações** dos apps Linux chegam pelo serviço de notificações padrão, que o `lord-server` fornece, e podem ser configuradas por app em Ajustes.
- **Senhas:** o Secret Service padrão é servido pelo chaveiro do LordOS, então os navegadores podem guardar seus segredos ali sem outro daemon de chaveiro.

[[shot:2]]

[[shot:3]]

## Quais protocolos Wayland existem

`wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output` (um por tela), `xdg_wm_base` com popups, `wl_seat` (ponteiro, teclado, toque), `wl_data_device_manager` (área de transferência), `zxdg_decoration_manager_v1`, o protocolo de decoração no servidor do KDE, `zxdg_output_manager_v1`, `wp_fractional_scale_v1`, `wp_viewporter`, `zwp_linux_dmabuf_v1`, `wp_presentation`, `zwp_relative_pointer_manager_v1`, `zwp_pointer_constraints_v1`, `zwp_pointer_gestures_v1` e `wp_cursor_shape_manager_v1`.

Ausente de propósito: qualquer protocolo que permita a um app capturar a tela ou as janelas de outros apps.

[[shot:4]]

## As perguntas do leitor cético

**Os apps Linux ficam em sandbox?** Não por padrão. Isso é importante: eles rodam como o seu usuário, como em qualquer sistema Debian, e podem ler sua pasta pessoal. O que o LordOS acrescenta é um cgroup por app, a atribuição no Monitor, o firewall por app aplicado pelo kernel (permitir, negar, só LAN, sem conexões de entrada, regras por domínio ou endereço) e restrições que você pode definir por app: sem câmera, sem USB e sem sockets Bluetooth (no kernel), sem microfone (o guarda do PipeWire do LordOS mais os nós de captura da placa de som), sem notificações e "sem acesso aos seus arquivos", aplicada a partir da próxima abertura. Elas não cobrem a área de transferência, o Bluetooth através do BlueZ no barramento do sistema nem programas iniciados fora do lançador de apps. Só os apps `.lpk` do próprio LordOS recebem o sandbox completo.

**Um app Linux consegue ver outras janelas ou gravar a tela?** Não pelo LordOS. O compositor só entrega a um app as superfícies dele mesmo e não oferece nenhum protocolo de captura de tela, apps X11 só veem outros apps X11 (nunca os apps Wayland nem o LordOS), não há portal de captura de tela, e um app restrito também tem o dispositivo de framebuffer negado, então janelas protegidas como a de Senhas também ficam privadas para eles.

**E os apps que só falam X11?** Desde o LordOS 0.8.3 Beta eles abrem pelo Xwayland, que só inicia quando um deles se conecta, com a moldura de janela do tema, o ícone no Dock e a área de transferência compartilhada. Eles têm a mesma identidade por app, o mesmo firewall e a mesma atribuição no Monitor que os apps Wayland; apps com restrições de dispositivos ou de arquivos não recebem tela X. Foram testados o XTerm, um app Java Swing e um jogo SDL.

**E o Flatpak?** O LordOS não o usa nem o integra, e ele não foi testado.

**Por que os pacotes vêm por HTTP?** O motor de pacotes do LordOS não tem TLS próprio, então as fontes `https://` são baixadas por `http://`. A segurança vem das assinaturas e dos hashes: todo índice precisa ter uma assinatura válida de uma chave confiável, não pode estar expirado nem ser mais antigo que o já guardado, e cada arquivo é conferido contra seu SHA-256 antes de ser usado. Alguém na rede consegue ver quais pacotes você baixa, mas não consegue alterá-los.

## Limites honestos

- **Apps X11 se veem entre si,** como em qualquer desktop X11; ficam nítidos só em 100 % de escala.
- **Ainda não:** arrastar e soltar entre apps, os métodos de entrada do LordOS (coreano, japonês, chinês) dentro dos apps Linux e os pedidos de ativação de apps.
- **Ícones só em SVG:** o LordOS ainda não desenha SVG. Ele usa os ícones PNG que o Debian publica para a maioria dos apps, ou a inicial.
- **Apps de terminal** com arquivo `.desktop` não aparecem no lançador de apps; inicie-os pelo Terminal.
- **Os apps GTK 3 mantêm o próprio destaque azul;** a leitura do esquema de cores pelo portal no Qt 6 não foi testada.

## Leia o código

- [server/src/wl/wl.c](https://github.com/lordbasex/LordOS/blob/main/server/src/wl/wl.c): o compositor Wayland e seus protocolos
- [server/src/apps.c](https://github.com/lordbasex/LordOS/blob/main/server/src/apps.c): o lançador de `.desktop`
- [server/src/ui/portal.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/portal.c): o portal de configurações para GTK e libadwaita
- [sdk/wayland.js](https://github.com/lordbasex/LordOS/blob/main/sdk/wayland.js): as janelas Linux no gerenciador de janelas
- [server/src/pkg](https://github.com/lordbasex/LordOS/tree/main/server/src/pkg): o motor de pacotes Debian
- [apps/store](https://github.com/lordbasex/LordOS/tree/main/apps/store): a Loja
- [docs/WAYLAND.md](https://github.com/lordbasex/LordOS/blob/main/docs/WAYLAND.md), [docs/PACKAGES.md](https://github.com/lordbasex/LordOS/blob/main/docs/PACKAGES.md), [docs/GPU.md](https://github.com/lordbasex/LordOS/blob/main/docs/GPU.md)
