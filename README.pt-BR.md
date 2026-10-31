![Call of Duty 2 VR](docs/images/banner.jpg)

# CALL OF DUTY 2 VR

[English](README.md) | **Português**

Um mod de VR para PC da **campanha single player do Call of Duty 2** (`CoD2SP_s.exe` 1.3). Jogue o jogo de 2005 no
seu headset: você olha em volta e se move com o próprio corpo, a arma fica na sua mão, você mira com o controle e vê
o HUD e os menus numa tela flutuante. Dos criadores do
[mod de VR do Call of Duty 1](https://github.com/lambda-gab/cod1-vr).

> **Aviso:** projeto de fã, não oficial, sem ligação com a Activision nem endossado por ela. Call of Duty é marca
> registrada da Activision Publishing, Inc. É preciso ter uma cópia legítima do jogo; nenhum código ou arquivo do
> jogo é incluído ou distribuído (os pacotes opcionais são gerados no seu PC a partir da sua cópia).

> [!WARNING]
> **Este projeto está em desenvolvimento.** Podem acontecer bugs e falhas visuais. Por favor, reporte no
> [Discord](https://discord.gg/SRsK2x5Tn).

## Apoie o desenvolvedor

> [!TIP]
> **Quer ajudar o dev? US$ 2 mantêm o plano Claude Pro funcionando.**
>
> Todo jogo portado é e sempre será gratuito para todos. Seu apoio paga o plano de IA e me deixa dedicar algumas
> horas do meu dia a levar alegria aos jogadores.
>
> [**Apoie no Patreon**](https://www.patreon.com/cw/LambdaGab)

### Apoiadores no Patreon

<table>
  <tr>
    <td valign="middle"><img src="docs/images/supporters/master.png" width="64" alt="Master"></td>
    <td valign="middle"><b>Dmitriy Melnikov</b><br>Master</td>
  </tr>
</table>

## Requisitos

- Call of Duty 2 para PC, single player, **versão 1.3** (`CoD2SP_s.exe`).
- Um headset de VR para PC com controles (testado com o Quest 3 pelo Virtual Desktop; Air Link e outros headsets
  de PC VR devem funcionar também).
- Um runtime OpenXR de **32 bits**: Virtual Desktop (VDXR), Oculus/Meta ou SteamVR (o runtime OpenXR de 32 bits
  vem com o **SteamVR beta**: Steam > SteamVR > Propriedades > Betas). O jogo é um programa de 32 bits.

> [!NOTE]
> **Testado com o Quest 3.** Se tiver problema com outro headset,
> [abra uma issue](https://github.com/lambda-gab/cod2-vr/issues) para eu poder olhar.

## Instalação

> [!IMPORTANT]
> **O VR não inicia, ou o jogo abre como duas imagens lado a lado no monitor?** Salve um **relatório de
> diagnóstico** (launcher > ABOUT > "Problem starting in VR?") e mande no [Discord](https://discord.gg/SRsK2x5Tn).

### Mais fácil: o instalador (recomendado)

1. Baixe o `CoD2-VR-Setup.exe` do [release mais recente](https://github.com/lambda-gab/cod2-vr/releases/latest).
2. Execute. Ele acha a pasta do jogo (ou deixa você escolher em **Alterar...**) e confere se o jogo é a versão 1.3.
3. Escolha se quer o atalho na área de trabalho e clique em **INSTALAR**.
4. Conecte o headset e abra o jogo pelo launcher **Call of Duty 2 VR** (o atalho, ou o `Call of Duty 2 VR.exe`
   na pasta do jogo).

Execute o instalador de novo para atualizar ou desinstalar. Suas configurações são mantidas nas atualizações.

### Manual

1. Extraia o `cod2-vr-manual-install.zip` do [release mais recente](https://github.com/lambda-gab/cod2-vr/releases/latest).
2. Copie `d3d9.dll`, `openxr_loader.dll`, `cod2_vr.ini`, `supporters.ini` e `Call of Duty 2 VR.exe` para a pasta do
   jogo (onde fica o `CoD2SP_s.exe`).
3. Conecte o headset, abra o `Call of Duty 2 VR.exe` e clique em PLAY.

Para desinstalar, apague esses cinco arquivos (e o `main\zz_cod2vr.iwd` / `main\zz_cod2vr_font.iwd`, se o launcher
os criou).

## Controles

A arma fica na sua mão dominante (escolha no launcher, CONTROLS). Armas e granadas são pegas do corpo com o grip de
qualquer mão:

| Ponha a mão aqui e aperte o grip | Você pega |
|---|---|
| Sobre o ombro direito (mão atrás da cabeça) | Arma 1 |
| Sobre o ombro esquerdo | Arma 2 |
| Atrás, na lombar | Granada explosiva (solte o grip para lançar, na direção do movimento) |
| No quadril esquerdo | Granada de fumaça (igual) |
| No peito | Binóculos (levantados enquanto o grip segura) |

Cada zona pode ser trocada no launcher (CONTROLS). Chegar numa zona dá uma vibração leve.

| Controle | Ação |
|---|---|
| Gatilho da mão da arma | Atirar. O tiro sai do cano da arma, na direção do cano |
| Grip de qualquer mão em algo para usar ou pegar | Usar (uma arma na mesa, um interruptor, explosivos para plantar) |
| Grip da mão da arma em outro lugar | Usar / pegar para onde a arma aponta |
| Grip da mão livre no guarda-mão da arma | Segurar a arma com as duas mãos (mais firme, menos recuo) |
| Gatilho da mão livre, com a mão em algo para usar | Usar |
| Bater com a arma num inimigo | Corpo a corpo: qualquer parte da arma, cano ou coronha |
| Costas da mão livre viradas para os olhos | Objetivos, como olhar um relógio |
| Analógico esquerdo | Andar (para onde você olha) |
| Analógico direito | Girar (45° por vez); para cima / baixo muda a postura |
| A / B | Pular / recarregar |
| X / Y | Granada / próxima arma. Segure Y e aperte A: troca entre a arma 1 e a 2 |
| Clique do analógico esquerdo | Recentralizar (fique de pé, reto); segurar: objetivos |
| Clique do analógico direito | Deitar |
| Menu | Menu do jogo |

- Uma arma na mão esquerda (configuração de canhoto) é espelhada.
- Agache de verdade e o soldado agacha. Fique de pé, reto, e clique o analógico esquerdo para recentralizar.
- O gatilho (ou o A) pula os filmes da campanha e começa a missão assim que ela termina de carregar.
- Os controles vibram quando você atira, leva dano e chega numa zona do corpo.

Nos menus, aponte com a mão direita: o gatilho clica, o B volta, o A confirma.

## Configurações

### O launcher

O launcher tem as configurações que a maioria dos jogadores quer, salvas assim que você muda:

- **GRAPHICS:** resolução do headset (com o tamanho por olho), antialiasing (MSAA), filtragem de texturas
  (anisotrópica), qualidade das texturas, fonte de texto moderna, brilho e pós-processamento (nitidez, estilo de cor,
  bloom).
- **VR:** movimento (contínuo, ou saltos Blink / Shift, beta), vinheta de conforto, tremor da câmera, giro em passos
  ou suave, roomscale, agachar físico, menus fixos ou seguindo a cabeça, o que do HUD fica na tela, tamanho da
  legenda, os braços do soldado (visíveis, beta, ou escondidos), estilo, cor e opacidade da mira, trajetória da
  granada, corpo a corpo batendo com a arma, firmeza e recuo com as duas mãos, objetivos na mão e força da vibração
  para tiros, dano e zonas do corpo.
- **CONTROLS:** o que cada zona do corpo guarda, a ação e os atalhos de cada botão, a mão dominante, qual analógico
  anda, com perfis.

As linhas marcadas *Coming soon* são opções do mod do Call of Duty 1 que ainda não estão neste mod.

Com a versão Steam do jogo, o PLAY abre o jogo pelo Steam.

As escolhas vão para `%APPDATA%\CoD2-VR\settings.ini` e têm prioridade sobre o `cod2_vr.ini`.

### cod2_vr.ini

Todo o resto fica no `cod2_vr.ini`, na pasta do jogo, com um comentário em cada configuração. Se o jogo estiver em
`Arquivos de Programas`, abra o editor (Bloco de Notas) como administrador para salvar. As mais usadas:

- `[Controllers]` `WeaponOffset*`: posição da arma na mão; `MuzzleDistance`: de onde o tiro sai da arma;
  `FireFromGun`: tiro do cano (1) ou do olho (0).
- `[Controllers]` `Crosshair`, `CrosshairColor`, `CrosshairOpacity`, `CrosshairSize`, `SwingMelee`, `MeleeSpeed`.
- `[Holsters]`: o que cada zona do corpo guarda e as alturas das zonas.
- `[VR]` `RenderScale`, `RoomScale`, `WorldScale`, `FxAlways` (efeitos e marcas no chão desenhados para onde a arma
  apontar).
- `[Comfort]`: a vinheta, `ShakeKeep`, `WeaponCamera` (a animação de recarga mexendo a visão), `SkipInvertMenu`.
- `[HUD]`: as telas flutuantes, `HudMode`, `MenuFollowsHead`, os objetivos na mão.
- `[Buttons]`: o comando de cada botão (`+cmd` fica pressionado enquanto o botão está apertado, `a|b` roda `a` ao
  apertar e `b` ao soltar).

O log é o `cod2_vr.log` (em `Arquivos de Programas` ele vai para `%LOCALAPPDATA%\VirtualStore\...`).

## Releases

### v1.0.3

- **Fechamento ao iniciar em alguns PCs (versão Steam, overlays):** toda a apresentação do quadro, com o overlay da
  Steam e qualquer outro overlay junto, agora roda na pilha grande do mod em vez da pilha minúscula do jogo.
- **Registro de crash no log:** se o jogo ainda fechar, o `cod2_vr.log` agora diz onde (o módulo e o erro), e o
  relatório de diagnóstico mostra a causa.

### v1.0.2

- **SteamVR, segunda correção do fechamento ao iniciar:** as threads que o runtime do SteamVR cria dentro do jogo
  também recebiam a pilha minúscula padrão do jogo. Toda thread nova do jogo agora recebe pelo menos 1 MB.

### v1.0.1

- **O SteamVR não fecha mais o jogo ao iniciar** (Valve Index e outros headsets do SteamVR): o jogo dá a cada thread uma
  pilha muito pequena e o runtime do SteamVR estourava. O trabalho de VR agora roda numa pilha própria.
- **Os tiros acertam a mira**: a mira fica onde a linha do tiro encontra o mundo, em qualquer distância, e com a arma
  perto do olho o tiro segue a linha do seu olho ao longo da arma.

### v1.0.0

O primeiro release.

- Imagem estéreo de cada olho rastreado, OpenXR (32 bits), rastreamento da cabeça com posição, roomscale (opcional).
- A arma na mão dominante, mirada com o controle; o tiro sai do cano, na direção do cano; uma mira onde ele vai
  acertar (estilo, cor, opacidade); segurar com as duas mãos.
- Os braços do soldado seguem os seus controles (beta), ou só a arma aparece; a arma é espelhada na mão esquerda.
- Armas e granadas no corpo: ombros, lombar, quadril esquerdo (fumaça), peito (binóculos), cintura.
- Corpo a corpo batendo com a arma; usar e pegar com qualquer mão; objetivos nas costas da mão.
- Movimento contínuo ou saltos Blink / Shift (beta), giro em passos ou suave, agachar físico, escadas, colisão da
  cabeça, vinheta de conforto, menos tremor da câmera, a animação de recarga não mexe mais a visão.
- HUD e menus numa tela flutuante, modos de HUD, tamanho da legenda, o painel de apoiadores ao lado do menu principal.
- A missão de treino adaptada ao VR: sem as mensagens de "mirar com o botão direito" e "inverter o mouse", o
  manequim derrubado com a arma, os filmes e briefings pulados com o gatilho.
- Efeitos (fumaça, fogo) e marcas no chão não somem mais quando a arma aponta para outro lado.
- Launcher com GRAPHICS, VR, CONTROLS (perfis), MODS e ABOUT, relatório de diagnóstico e instalador.

## Compilando

Visual Studio com as ferramentas de C++ (x86) e o loader OpenXR da Khronos em `deps/openxr/`
(`openxr_loader_windows-<versão>.zip` dos
[releases do OpenXR-SDK-Source](https://github.com/KhronosGroup/OpenXR-SDK-Source/releases)).

- `proxy\build.bat` → `proxy\out\d3d9.dll`, `openxr_loader.dll`, `cod2_vr.ini`
- `launcher\build_launcher.bat` → `launcher\out\Call of Duty 2 VR.exe`
- `installer\build_setup.bat` → `installer\out\CoD2-VR-Setup.exe` (embute os dois acima)

Uma tag de versão (`v1.0.0`) compila tudo no GitHub Actions e publica o release.

## Notas técnicas

Como foi feita a engenharia reversa e como o estéreo funciona: [docs/CoD2_VR_RE.md](docs/CoD2_VR_RE.md).
