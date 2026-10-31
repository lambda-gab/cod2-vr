# Call of Duty 2 SP → VR: engenharia reversa do CoD2SP_s.exe

Ferramentas: REA 6.5.0 (`rea-agents`) com o provider Ghidra 12.1.4 (JDK 21), capstone e `tools/redis.py`
(disassembly, strings, xrefs por imediato). Alvos:

- `CoD2SP_s.exe` 1.3 (timestamp `0x442ca3f7`), base `0x400000`, sem ASLR. Endereços absolutos.
- `gfx_d3d_x86_s.dll` (timestamp `0x4456a390`), base preferida `0x10000000`, **com relocações**: no jogo ela
  carregou em `0x13270000`–`0x13e10000`. Todo endereço da DLL é convertido com a base real (`GFX(addr)`).

Endereços em `proxy/cod2_addresses.h`.

## Diferenças em relação ao CoD1

| | CoD1 (CoDSP.exe 1.5) | CoD2 (CoD2SP_s.exe 1.3) |
|---|---|---|
| API gráfica | OpenGL 1.x (fixed function) | Direct3D 9 (shaders) |
| Renderer | dentro do exe | `gfx_d3d_x86_s.dll`, carregada por `LoadLibrary("gfx_%s_x86_s.dll")` + `GetRefAPI` |
| Proxy | `opengl32.dll` | `d3d9.dll` (o exe e a DLL importam `Direct3DCreate9`) |
| Estéreo nativo | caminho de estéreo do Q3 intacto | não existe |
| cgame | `cgamex86.dll` | dentro do exe |
| Backend | síncrono | thread SMP opcional (`r_smp_backend`, padrão 1) |

## Tabela `re` (refexport_t)

`GetRefAPI` (`0x10013430`) preenche 108 ponteiros em `0x10748e00`; `CL_InitRef` (`0x40da80`) copia a tabela para
**`0x617ab0`** no exe (`rep movsd` em `0x40db33`, `0x6c` dwords). O mod troca ponteiros nessa cópia.

| Offset | Função | Endereço na DLL | Como foi identificada |
|---|---|---|---|
| `+0x08` | RegisterModel | `0x1001daa0` | string `R_RegisterModel: Invalid model name` |
| `+0x10` | RegisterShader/Material | `0x10017500` | 80 chamadores com nomes de material |
| `+0x48` | ClearScene | `0x10025960` | chamado pelo `SCR_UpdateScreen` após o BeginFrame |
| `+0x94` | **RenderScene** | `0x10025bf0` | string `====== R_RenderScene ======` |
| `+0xac` | DrawStretchPic | `0x10021560` | emite o comando `0xf`; 52 chamadores |
| `+0xc0` | **BeginFrame** | `0x100223a0` | `SCR_UpdateScreen` (`0x40fb00`) |
| `+0xc4` | **EndFrame** | `0x100223f0` | idem; emite e executa os comandos |
| `+0xe0` | ClearScreen | `0x10022670` | `(flags, cor, depth, stencil)` |

`re.RenderScene` é chamado em `0x4bfccd` por `CG_DrawActive` (`0x4bfbf0`, a vista do mundo; retorno `0x4bfcd3`) e
em `0x402087` (outras cenas). O `refdef` do cgame fica em **`0x00f79138`**.

### refdef_t

Mesmo layout do Q3 no começo: `x, y, width, height`, `fov_x, fov_y` (graus), `vieworg[3]` (`+0x18`),
`viewaxis[3][3]` (`+0x24`), `time` (`+0x48`), `zNear` (`+0x4c`, 0 = `r_znear`), `+0x50`, `+0x54`.

## Frontend

- `R_RenderScene` pega o próximo `viewParms` (`frontEndData` em `0x101da9e8`, contagem `+0x169f3c`, array
  `+0x169f40`, `0x14c` bytes cada) e chama `R_SetupViewParms` (`0x100259c0`, refdef na pilha, viewParms em **EBX**).
- `R_SetupViewParms` zera o viewParms e preenche: origem, eixos, viewport (`+0x30`), view matrix (`+0x48`,
  `0x1005c0b0`), projeção (`+0x88`) e viewProj (`+0xc8`, `0x1005ac80`).
- `R_SetupProjection` (`0x1005bfe0`, `(fov_x, fov_y, zNear)`, matriz em **ESI**): projeção infinita D3D, mão
  esquerda, espaço de vista x direita, y cima, z frente: `m[0] = cot(fov_x/2)·0.9995`, `m[5] = cot(fov_y/2)·0.9995`,
  `m[10] = 0.9995`, `m[11] = 1`, `m[14] = -0.9995·zNear`. A chamada de `R_SetupViewParms` está em `0x10025aa8`.
  Frustum assimétrico de um olho: `m[0] = 2/(r-l)`, `m[8] = -(r+l)/(r-l)`, `m[5] = 2/(u-d)`, `m[9] = -(u+d)/(u-d)`.
- `R_RenderScene` também copia `vieworg` e `viewaxis[0]` para `0x101d4d04` (6 floats).

## Fila de comandos e backend

O frontend grava comandos `{u16 id, u16 tamanho, dados}` no buffer do frame (`0x10366a38`, usado em `+0xc000`).
Os handlers ficam em **`0x10190518`** (34 entradas, `cdecl handler(ctx)`, `ctx = { BYTE *cur, int depth,
BYTE *pilha[] }`), executados por `RB_ExecuteRenderCommands` (`0x100340d0`, laço em `0x10034174`).

| Id | O quê |
|---|---|
| 1 | salto (`cur = [cur+4]`) |
| 2 | chamada (empilha o retorno) |
| 3 | retorno |
| 12 | começo de uma vista: copia viewport e viewProj **do viewParms apontado** (`cmd+0x14`) |
| 13 | viewport `(x, y, w, h)`, emitido por `R_AddCmdSetViewport` (`0x10022050`, cdecl) |
| 14 | render target `(id)` → `RB_SetRenderTarget` (`0x1003ee80`, id em EAX) |
| 15 / 21 | 2D (stretch pic / texto) |

**Os estágios de cada vista são gravados em segmentos pulados (1) e chamados no fim do frame (2) numa ordem fixa**
(floatz → shadow maps → cena → efeitos → 2D). Os "slots" de cada estágio da vista principal são estáticos por
frame (`0x10675264..0x10675288`): duas `RenderScene` de mundo no mesmo frame corrompem a lista (crash em
`0x10034187`, ponteiro de comando inválido). Visto com o trace de comandos (`[Debug] TraceCommands=1`).

### Render targets

`dx.renderTargets[13]` em **`0x101d4820`**, `{ image*, colorSurface, depthStencilSurface, width, height }`:
0 framebuffer (o backbuffer), 1 resolved_post_sun, 2 resolved_scene, 3 dynamic_shadows, 6 floatz, 7 savedscreen,
8 blurred_screen, 9–10 glow, 11–12 pingpong. `RB_SetRenderTarget` guarda o id atual em `0x10744c98` (14 = nenhum)
e a superfície ligada em `0x10744ca8`/`0x10744cac`; só religa se o id mudou. `dx.device` em `0x101d1bf8`.

`RB_SwapBuffers` (`0x10033eb0`) apresenta pela swap chain (`IDirect3DSwapChain9::Present`).

## Solução de estéreo: frontend uma vez, backend duas vezes

1. `hk_RenderScene` (só a vista do `CG_DrawActive`) calcula o refdef de cada olho e chama o `RenderScene` original
   **uma vez**, do meio dos olhos, com o FOV de culling da união dos olhos + margem, entre dois marcadores.
2. Marcadores: comandos 13 com `x = 0x565232nn` (impossível), emitidos por `R_AddCmdSetViewport`. O handler 13 é
   envolvido:
   - `WORLD_BEGIN`: liga a textura do olho esquerdo em `renderTargets[0]` (cor, depth, tamanho), força a religação
     (`0x10744c98 = 14` + `RB_SetRenderTarget(0)`), limpa, e refaz o viewParms do mundo para o olho esquerdo
     chamando o próprio `R_SetupViewParms` (com a projeção assimétrica). Guarda o cursor de comandos.
   - `WORLD_END` (1ª vez): olho direito, viewParms refeito, e o cursor volta para depois do `WORLD_BEGIN`
     (`depth = 0`): **a lista inteira do mundo é executada de novo**, com shadow maps e efeitos.
   - `WORLD_END` (2ª vez): liga o HUD (VR) ou compõe os olhos lado a lado (sem headset); o 2D segue uma vez.
   - `HUD` (no BeginFrame, em VR): todo o 2D vai para a textura do HUD (alpha 0 de fundo).
3. Viewports em pixels da janela são reescalados para o tamanho do olho enquanto um olho está ligado.

Validado no jogo (22/10/2026, GTX 970, sem headset): lado a lado com os dois olhos completos (modelos, sombras,
efeitos), e o modo `FakeHeadset` (olhos 1440×1584 com FOV assimétrico tipo Quest 3, HUD separado).

## D3D9 → OpenXR

OpenXR não tem binding D3D9. O proxy cria o objeto como `IDirect3D9Ex` e o dispositivo com `CreateDeviceEx`;
recursos `D3DPOOL_MANAGED` (proibido no 9Ex) viram `D3DPOOL_DEFAULT` (texturas com `D3DUSAGE_DYNAMIC` para o
`LockRect` do jogo continuar funcionando). Validado: menus, fases e texturas carregam normalmente.
`vr_xr.c` usa `XR_KHR_D3D11_enable` num dispositivo D3D11 próprio (adaptador pelo LUID do runtime) e cria texturas
`B8G8R8A8` compartilhadas (`D3D11_RESOURCE_MISC_SHARED`) que o 9Ex abre como render targets. No `Present`:
flush do D3D9 (query de evento), cópia para as swapchains (`B8G8R8A8_UNORM_SRGB`), `xrEndFrame`, e o próximo
`xrWaitFrame/xrBeginFrame/xrLocateViews` (ritmo do headset), já com a pose para o frame seguinte.

## Cliente: ângulos, movimento, menus

| O quê | Endereço | Como foi achado |
|---|---|---|
| `cl.viewangles[3]` | `0x00621584` | escrito por `CL_AdjustAngles` (`0x409400`, lê `cl_yawspeed` `0x5d1f90`) e `CL_MouseMove` (`0x409720`) |
| `CL_CreateCmd` | `0x00409da0` | único chamador de `CL_MouseMove` |
| `CL_FinishMove` | `0x00409d00` | usercmd em **EBX**; chamada em `0x409e27` (desviada para somar o analógico) |
| `usercmd.forwardmove/rightmove` | `+0x18` / `+0x19` | `CL_KeyMove` (`0x409688`), `signed char` |
| `Cbuf_AddText` | `0x0041afc0` | texto em **EAX** (chamada com `"snd_restart\n"` em `0x42753e`) |
| `cls.keyCatchers` | `0x00617f04` | bit 8 = UI (`CL_MouseEvent` desvia para `UI_MouseEvent`) |
| `CL_MouseEvent` | `0x004096d0` | dx em EAX, dy em EDX; chamado por `IN_MouseMove` (`0x44ef70`, `GetCursorPos`) |
| cursor da UI | `0x010fec3c` / `0x010fec40` | `UI_MouseEvent` (`0x4c6470`), limitado a 640×480 |

Teclas e cliques vão como `WM_KEYDOWN/UP` e `WM_LBUTTONDOWN/UP` para a janela do jogo (o jogo não usa DirectInput).
Validado: Esc abre o menu; o cursor posicionado em (395, 300) destaca o item "Multiplayer".

Comandos de botão do CoD2 (tabela em `0x5a4e80`): `+attack +activate +reload +usereload +melee +frag +smoke
toggleads leaveads +speed +gostand raisestance lowerstance toggleprone +prone +leanleft +leanright weapnext
weapprev "weaponslot primary" "weaponslot primaryb" +binoculars +holdbreath +scores`.

## Segunda rodada (após o primeiro teste no headset)

- **Abertura:** o `Com_Init` enfileira `unskippablecinematic legal` (string em `0x575ce4`, copiada com tamanho fixo)
  e, se `com_introPlayed` (cvar em `0x74a920`) for 0, `cinematic atvi` e `IW_logo`. O mod troca a primeira por
  espaços (mesmo tamanho) e torna incondicional o `jne` de `0x42e7e0` (`75 67` → `EB 67`). `[General] SkipIntro`.
- **Braços:** só os 9 `xmodel/viewmodel_hands*` têm materiais de braço (as armas não). O xmodel tem o mesmo
  formato do CoD1 (`ff ff ff ff 00 00 00 00`, `u16` nº de materiais, nomes com NUL). `tools/build_iwd.py` gera
  `zz_cod2vr.iwd` com esses modelos apontando para `mtl_vr_invisible`: cópia do material `hill400_treeline01`
  (técnica `phong_alphatest`) com as strings refeitas — cabeçalho de `0x5c` bytes, nome em `+0x00`, imagem de
  referência `+0x04`, técnica `+0x38`, tabela de texturas `+0x3c` com `+0x34` entradas `{semântica, flags, imagem}` —
  e `colorMap` = `images/vr_invisible.iwi` (IWi v5, DXT1 4×4, `color0 = color1 = 0`, índices `0xFF`: tudo
  transparente). Validado no jogo (`demolition`): só a arma aparece. O jogo carrega um `.iwd` com qualquer nome em `main`.
- **Flick da arma:** os ângulos da cabeça entram em `cl.viewangles` no `Present`, ou seja, antes do `CL_CreateCmd`
  do frame seguinte (no CoD1 era depois). O yaw da cabeça já embutido na vista e o pitch enviado eram guardados
  um frame atrasados: o corpo girava pela diferença do movimento da cabeça a cada frame e o offset de pitch oscilava.
  Agora são lidos no `CL_FinishMove` (`VR_OnCreateCmd`), quando o usercmd daquele frame é feito.
- **Linha dos menus:** `overlay.c`, camada de projeção extra com uma swapchain por olho. A geometria é projetada na
  CPU com a pose e o FOV de cada olho; os shaders (compilados com `d3dcompiler_47.dll`) só repassam posição e cor.
  Validado fora do jogo com `proxy/test/overlaytest.c` (o fim da linha cai no pixel calculado).

## Terceira rodada (29/10/2026): arma na mão, tiro, playerState

- **Arma da vista:** `cg_gun_x/y/z` (cvar_t* em `0xf569d4` / `0xf48b58` / `0xf56994`) só são lidos por
  `CG_AddViewWeapon` (`0x4b6d80`). Ele monta a entidade da arma na pilha (eixos em `+0x14`, origem em `+0x3c`) e chama
  `0x4b69b0(ps, 0xf78ea8, ?)` com a entidade em **ESI** (chamada em `0x4b6ecc`); essa função lê `+0x14..+0x44` e passa
  ao renderer (`re+0x4c`/`re+0x50`). O mod envolve a chamada (`hk_AddPlayerWeapon`). No jogo (`demolition`, sem headset,
  `FakeHand`): a origem que o jogo dá à arma é a própria origem da vista (0, 0, 0): **os modelos de arma do CoD2 têm a
  origem no olho**, e `cg.refdef` já tem a vista do frame quando a arma é adicionada.
- **playerState:** o primeiro argumento de `0x4b69b0` é `0xf708e8` (`cg.predictedPlayerState`; o `CG_FireWeapon` também
  o usa). Origem em `+0x14` (layout do Q3): `vieworg.z - origem.z = 60,0` em pé. `stats` em `+0x10c`
  (`MSG_ReadDeltaPlayerstate` `0x43d810`, string `PS_STATS`), mas **`stats[0]` não é a vida** (0 275 0 0 com vida cheia).
- **Tiro:** `CG_FireWeapon` (`0x4b7800`, string de erro em `0x580181`, chamado pelo `CG_EntityEvent` para os
  `EV_FIRE_WEAPON*`) compara `cent+0xa8` (número da entidade) com `cg.snap` (`0xf5bd28`) `+0xb0` (cliente do jogador).
  O prólogo `83 ec 10 53 55` (5 bytes) é desviado (`hk_FireWeapon`); validado: a linha `shot:` aparece no log após um
  `+attack` do `[Debug] TestFire`.
- Tabela de `0x1b8` bytes por arma em `0xf48d80`, indexada pelo byte de arma da entidade (`cent+0xa2`): provavelmente
  `cg_weapons`.

## Quarta rodada (29/10/2026, tarde): entidades, dano, armas, telas cheias, morte

Tudo em `proxy/cod2_addresses.h`; o resumo em inglês está em `docs-handoff/ENGINE_DIFFERENCES.md`.

- **`g_entities`** em `0x1b15f00`, `0x1fc` bytes cada (método de script `dodamage` `0x4efaf0`; o atacante padrão do
  `G_Damage`, `0x1c13b08`, é a entidade 1022). Campos: origem `+0xdc`, cliente `+0xfc`, ator (IA) `+0x100`,
  `takedamage` `+0x111`, vida `+0x13c`.
- **`G_Damage`** `0x528050(alvo, causador, atacante, dir*, ponto*, dano, dflags, mod, hitLoc)`; `MOD_MELEE` = 7;
  hitLoc 2 cabeça, 4 torso. Usado pelo golpe com a arma: funciona no jogo (IA morta por golpes, log do mantenedor).
- **Definições de arma**: `bg_weaponDefs` `0x1c64548`, quantidade `0x1cbad9c`; tabela de campos `{nome, deslocamento,
  tipo}`: `weaponType` `+0x78` (0 bala, 1 granada), `cookOffHold` `+0x33c`, `hipSpread*` `+0x28c..+0x2b8`, `adsSpread`
  `+0x4bc`. As granadas do SP vêm com `cookOffHold 0` (lançadas logo após o pino, segurando ou não); o mod liga 1 em
  memória. A dispersão sem mirar (M1 Garand 2..8°, +8 andando) passa a ser a de mira (0,2°). Ambos confirmados no jogo.
- **Tela de morte**: o servidor manda `opendeadscreen <ui_deadquote>` (`0x4e8f19`); o cgame (`0x4a6ad0`) grava
  `cg.time` (`0xf708dc`) em `0xf7966c` e 0 no `closedeadscreen`. A vida da entidade 0 **não** chega a 0 na morte.
- `sv_running` (cvar_t* `0x747f04`): fase rodando. `Sys_QueEvent` `0x4500d0` (tempo em EAX, tipo 1 = tecla): o botão
  Menu precisa dele, as mensagens de janela não chegam sem foco.
- **Renderer**: `re.DrawStretchPic` = `0x10021560` (x, y, w, h, s0, t0, s1, t1, cor*, material*; nome do material em
  `+0`). O 2D de um quadro é enfileirado antes do mundo desse quadro; o backend executa o mundo antes do 2D; o 2D converte
  pixels pelo tamanho do alvo ligado (`0x1003eae0`: só alvos do tamanho da janela servem); os comandos 13/14 esvaziam o lote
  2D (`RB_EndTessSurface` `0x1003ad10`) antes de trocar de alvo.

## Ainda não verificado / próximos passos

- **Nada foi testado com headset**: sessão OpenXR, cópia das texturas compartilhadas, layers, poses e controles
  reais. O caminho até a submissão foi validado com o `FakeHeadset`.
- Arma na mão (viewmodel), coldres no corpo, mira pelo controle, granada, luneta, roomscale, agachamento físico,
  vibração, veículos/torretas, vinheta, MODS: nada disso foi portado ainda.
- `r_glow`, `r_aasamples` > 1 e outros efeitos de tela com olhos de tamanho diferente da janela.
- `vid_restart` com headset.
