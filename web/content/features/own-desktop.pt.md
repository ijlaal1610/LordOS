## Em poucas palavras

O LordOS é uma distribuição GNU/Linux baseada no Debian 13. O que o torna diferente é o desktop: em vez de trazer o GNOME ou o KDE, o LordOS tem um ambiente de desktop próprio, construído a partir dos seus componentes fundamentais sobre o Debian e o Wayland.

[[shot:0]]

Pense numa casa. O Debian e o kernel Linux são a fundação, o encanamento e a fiação: são reaproveitados, bem testados e mantidos por uma grande comunidade. O LordOS constrói os cômodos onde você de fato vive: a tela que você vê, as janelas que você move, os apps que você abre e os guardas nas portas.

[[diagram:layers]]

## O que é nosso e o que é reaproveitado

Aqui a precisão importa, porque é fácil exagerar no "próprio".

**Escrito para o LordOS (neste repositório):**

- **O servidor gráfico e compositor Wayland** (`lord-server`, em C). Ele define os modos de tela usando diretamente a interface DRM/KMS do kernel, sem libdrm; lê teclados, mouses, touchpads e telas sensíveis ao toque pelo evdev, sem libinput; compõe todas as janelas; e fala o protocolo Wayland com os apps Linux.
- **O motor de interface.** O desktop e os apps nativos são escritos em JavaScript com React. Eles rodam no QuickJS (um motor JavaScript pequeno, incluído como código-fonte) dentro do `lord-server`, com uma árvore de nós nativa, um motor de layout flexbox, um rasterizador 2D e um motor de texto escritos em C. Não há nenhum motor de navegador web por trás da interface.
- **Os apps nativos:** Arquivos, Ajustes, Terminal, Loja, E-mail, Calendário, Contatos, Lembretes, Senhas, Monitor, Fotos, Player, Visualizador de PDF, Discos, Backups e os demais, todos apps React no mesmo motor.
- **Os serviços de segurança:** o lançador de sandbox dos apps, o broker de permissões, o armazenamento de permissões por app, o serviço de firewall (`lord-netperm`), a assinatura de pacotes e o cofre de senhas (`lord-vault`).
- **Serviços do sistema** como o gerenciador de sessões e a tela de login (`lord-sessiond`), o serviço de rede (`lord-netd`) e o serviço de ventoinhas (`lord-fand`).

**Reaproveitado do Debian e do mundo do software livre:**

- o kernel Linux e os pacotes do Debian (o `dpkg` continua desempacotando e configurando cada pacote);
- o Mesa, para a composição opcional na GPU;
- o PipeWire, para o som;
- o próprio protocolo Wayland, via `libwayland-server` e `libwayland-client`;
- a `libxkbcommon`, para os layouts de teclado;
- bibliotecas usadas só dentro de processos ajudantes em sandbox: FFmpeg (mídia), Poppler e libarchive (documentos), OpenSSL (TLS para e-mail e área de trabalho remota), Monocypher (a criptografia do cofre).

A regra que o projeto segue está escrita em `docs/ARCHITECTURE.md`: X11, wlroots, libdrm, libinput, WebKit, GTK e Qt não são usados dentro do próprio LordOS. Apps GTK e Qt continuam rodando, como clientes Wayland.

## Como um quadro chega à tela

Veja o que acontece, passo a passo, quando você passa o mouse sobre o Dock:

1. O kernel entrega um evento de entrada pelo evdev. O `lord-server` o lê e descobre qual nó da interface está sob o ponteiro.
2. Ele chama o tratador de eventos em JavaScript. O React atualiza seu estado e informa ao motor quais nós mudaram: um estilo, um filho, um texto.
3. O motor recalcula o layout e determina a região alterada: só a parte da tela que realmente mudou.
4. Ele repinta só essa região, copia para o buffer traseiro, desenha o cursor e troca a página no próximo retraço vertical.

Se houver uma GPU com aceleração 3D, a composição passa pelo Mesa (GBM, EGL e GLES2). Se não houver, ou se o único renderizador for por software, o caminho da CPU faz o trabalho. O caminho da CPU precisa continuar funcionando sempre: é uma regra rígida no código (`LORD_GPU=0` o força), e a opção "modo seguro" do grub usa esse caminho.

[[diagram:frame]]

## Onde os apps Linux se encaixam

Há três tipos de janela num desktop LordOS, e elas se conectam em camadas diferentes:

- **Os apps incluídos e o tema** (barra de menus, Dock, Arquivos, Ajustes…) rodam dentro do próprio contexto JavaScript do `lord-server`, no motor de interface.
- **Os apps `.lpk` instalados** rodam cada um como um processo separado (`lord-server --app <id>`, o mesmo binário em "modo app"). Esse processo tem sua própria cópia do motor de interface, se tranca num sandbox e desenha num buffer de memória compartilhada, que entrega ao compositor como um cliente Wayland comum.
- **Os apps Linux** (GTK, Qt, Chromium, Firefox…) se conectam ao socket Wayland do compositor. Eles se desenham sozinhos; o tema coloca sua moldura de janela em volta deles.

Ou seja, o compositor é o ponto de encontro. O motor de interface é usado pela interface e pelos apps do próprio LordOS, não pelos apps Linux.

[[shot:3]]

## Leve por projeto

O LordOS mira máquinas modestas. Algumas medições reais dos registros de teste do projeto:

- A ISO final 0.7.0 Beta, ociosa um minuto após o login numa VM com 4 GB e 4 núcleos: **465 MB de RAM em uso** para o sistema inteiro na sessão live e **487 MB** no sistema instalado, com a CPU **99 % ociosa**. O boot levou 16,3 s (`docs/ISO.md`, "Final ISO test").
- Num Raspberry Pi 5 (16 GB), na tela de login e sem monitor: `lord-server` com **36 MB de RSS** e 0,6 % de CPU (`docs/RASPBERRY.md`).
- Quando a máquina não tem placa com 3D, o `lord-server` nem carrega o Mesa e fica em torno de 32 MB de RSS (`docs/GPU.md`).

Esses números vêm de máquinas virtuais e de uma placa, não de uma variedade de PCs reais. São um ponto de partida, não uma promessa para toda máquina.

[[shot:2]]

## Várias telas, texto nítido

O compositor controla várias telas ao mesmo tempo, estendidas ou espelhadas, e desenha cada uma na sua própria escala: 100, 125, 150, 175 ou 200 %. Texto, vetores e imagens são rasterizados na resolução real de cada tela, então um painel 4K a 150 % ao lado de um 1080p a 100 % fica nítido nos dois. Os apps Linux recebem um `wl_output` por tela e o protocolo de escala fracionária, então GTK 4, Qt 6 e Chromium também renderizam com nitidez.

[[shot:1]]

## Limites honestos

- **GPUs reais ainda não foram verificadas.** O caminho da GPU foi exercitado com o renderizador por software do Mesa em máquinas virtuais. O teste em placas AMD e Nvidia reais ainda está pendente (`docs/GPU.md`).
- **Várias telas foram testadas com duas saídas virtuais**, não com monitores reais, painéis de notebook ou hubs DisplayPort (`docs/DISPLAYS.md`).
- **O Raspberry Pi 5 ainda não foi testado com um monitor conectado.** A compilação, os testes e a tela de login rodaram numa placa real sem HDMI (`docs/RASPBERRY.md`).
- **O Xwayland não faz parte do LordOS**, então apps que só falam X11 não rodam. Arrastar e soltar entre apps Linux, a seleção primária e os métodos de entrada do LordOS dentro de apps Linux estão pendentes (`docs/WAYLAND.md`).
- **A acessibilidade ainda está quase toda pela frente:** hoje existe um ajuste para "reduzir movimento"; leitor de tela, zoom e alto contraste estão no roadmap.

## Leia o código

- [docs/ARCHITECTURE.md](https://github.com/lordbasex/LordOS/blob/main/docs/ARCHITECTURE.md): as camadas e a política de dependências
- [server/src/kms.c](https://github.com/lordbasex/LordOS/blob/main/server/src/kms.c): modos de tela com ioctls DRM, sem libdrm
- [server/src/input.c](https://github.com/lordbasex/LordOS/blob/main/server/src/input.c): entrada via evdev
- [server/src/gpu/gpu.c](https://github.com/lordbasex/LordOS/blob/main/server/src/gpu/gpu.c): composição opcional na GPU com Mesa
- [server/src/ui/layout.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/layout.c) e [server/src/ui/paint.c](https://github.com/lordbasex/LordOS/blob/main/server/src/ui/paint.c): layout e pintura
- [runtime/index.js](https://github.com/lordbasex/LordOS/blob/main/runtime/index.js): o renderizador React para o motor nativo
- [server/src/wl/wl.c](https://github.com/lordbasex/LordOS/blob/main/server/src/wl/wl.c): o compositor Wayland
- [docs/GPU.md](https://github.com/lordbasex/LordOS/blob/main/docs/GPU.md), [docs/DISPLAYS.md](https://github.com/lordbasex/LordOS/blob/main/docs/DISPLAYS.md), [docs/ISO.md](https://github.com/lordbasex/LordOS/blob/main/docs/ISO.md)
