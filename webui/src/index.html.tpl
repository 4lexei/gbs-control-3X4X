<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <title>GBS-Control</title>
    <link rel="manifest" href="${manifest}" />
    <style>
      ${styles}
    </style>
    <meta name="apple-mobile-web-app-capable" content="yes" />
    <link rel="icon" type="image/png" href="${favicon}" />
    <link rel="apple-touch-icon" href="${icon1024}" />
    <meta name="apple-mobile-web-app-status-bar-style" content="black" />
    <meta
      name="viewport"
      content="viewport-fit=cover, user-scalable=no, width=device-width, initial-scale=1, maximum-scale=1"
    />
  </head>
  <body tabindex="0" class="gbs-help-hide gbs-output-hide">
    <div class="gbs-container">
      <div class="gbs-menu">
        <svg
          version="1.0"
          xmlns="http://www.w3.org/2000/svg"
          viewBox="0,0,284,99"
          class="gbs-menu__logo"
        >
          <path
            fill-rule="evenodd"
            clip-rule="evenodd"
            fill="#010101"
            d="M283.465 97.986H0V0h283.465v97.986z"
          />
          <path
            fill-rule="evenodd"
            clip-rule="evenodd"
            fill="#3cd278"
            d="M270.062 75.08V60.242h-17.04v10.079c0 2.604-2.67 5.02-5.075 5.02h-20.529c-4.983 0-5.43-4.23-5.43-8.298v-37.93c0-2.668 1.938-4.863 4.88-4.863h20.995c2.684 0 5.158 1.492 5.158 4.482V38.86h17.04V27.63c0-7.867-4.26-15.923-13.039-15.923H221.19c-7.309 0-15.604 4.235-15.604 12.652v50.387c0 6.508 4.883 13.068 12.42 13.068h38.47c6.606 0 13.587-5.803 13.587-12.734zM190.488 5.562H6.617L6.585 91.91h183.91l-.007-86.348z"
          />
          <text
            transform="translate(12.157 81.95)"
            fill="#010101"
            font-family="'AmsiPro-BoldItalic'"
            font-size="92.721"
            letter-spacing="-9"
          >
            GBS
          </text>
          <g>
            <path
              fill-rule="evenodd"
              clip-rule="evenodd"
              fill="#010101"
              d="M586.93 97.986H303.464V0h283.464v97.986z"
            />
            <path
              fill-rule="evenodd"
              clip-rule="evenodd"
              fill="#FFF"
              d="M573.526 75.08V60.242h-17.04v10.079c0 2.604-2.669 5.02-5.075 5.02h-20.528c-4.984 0-5.43-4.23-5.43-8.298v-37.93c0-2.668 1.937-4.863 4.88-4.863h20.995c2.683 0 5.157 1.492 5.157 4.482V38.86h17.04V27.63c0-7.867-4.26-15.923-13.038-15.923h-35.833c-7.31 0-15.605 4.235-15.605 12.652v50.387c0 6.508 4.884 13.068 12.42 13.068h38.471c6.606 0 13.586-5.803 13.586-12.734zM493.953 5.562H310.08l-.032 86.348h183.91l-.006-86.348z"
            />
            <text
              transform="translate(315.621 81.95)"
              fill="#010101"
              font-family="'AmsiPro-BoldItalic'"
              font-size="92.721"
              letter-spacing="-9"
            >
              GBS
            </text>
          </g>
        </svg>
        <button
          gbs-section="imagen"
          class="gbs-button gbs-button__menu gbs-icon"
          active
        >
          tv
        </button>
        <button
          gbs-section="ajuste"
          class="gbs-button gbs-button__menu gbs-icon"
        >
          open_with
        </button>
        <button
          gbs-section="color"
          class="gbs-button gbs-button__menu gbs-icon"
        >
          palette
        </button>
        <button
          gbs-section="filters"
          class="gbs-button gbs-button__menu gbs-icon"
        >
          blur_on
        </button>
        <button
          gbs-section="presets"
          class="gbs-button gbs-button__menu gbs-icon"
        >
          input
        </button>
        <button
          gbs-section="system"
          class="gbs-button gbs-button__menu gbs-icon"
        >
          bolt
        </button>
      </div>
      <nav class="gbs-tabs" id="gbsTabs">
        <button data-group="modo" active>Modo</button>
        <button data-group="ajuste">Ajuste</button>
        <button data-group="imagen">Imagen</button>
        <button data-group="avanzado">Avanzado</button>
        <button data-group="sistema">Sistema</button>
      </nav>
      <div class="gbs-scroll">
        <div class="gbs-head">
          <div class="gbs-statusbar" id="gbsStatusBar">esperando estado…</div>
          <div class="gbs-subtabs" id="gbsSubtabs" hidden></div>
        </div>
        <section name="imagen">
          <div class="gbs-work">
            <div class="gbs-card">
              <div class="gbs-card__head">
                <div class="gbs-card__title" id="workMode">Presets disponibles</div>
                <div class="gbs-asp" id="workAspect" hidden>
                  <button data-asp="4:3">4:3</button>
                  <button data-asp="16:9">16:9</button>
                </div>
              </div>
              <div class="gbs-card__desc" id="workDesc">Elige un modo de salida abajo.</div>
              <div class="gbs-card__chips" id="workSteps" hidden>
                <button gbs-message=")" gbs-message-type="user" gbs-click="normal" class="gbs-chip" data-step="0"><span>cuadrado</span><small>256 px, píxel exacto</small></button>
                <button gbs-message="(" gbs-message-type="user" gbs-click="normal" class="gbs-chip" data-step="1"><span>CRT</span><small>aspecto de TV</small></button>
                <button gbs-message="46" gbs-message-type="img" gbs-click="normal" class="gbs-chip" data-step="2"><span>ancho</span><small>experimental</small></button>
                <button gbs-message="47" gbs-message-type="img" gbs-click="normal" class="gbs-chip" data-step="3"><span>llenar</span><small>experimental</small></button>
              </div>
              <!-- (after v107r) his text: the edge is kept in every mode, not only with 240 -->
              <div class="gbs-note" id="workEdgeNote" hidden><span class="gbs-note__ico">&#9650;</span><span>Ajusta el borde para corregirlo si se come parte del juego o úsalo para tapar basura (menú AJUSTE). Se guardará automáticamente.</span></div>
              <div class="gbs-lines" id="workLines" hidden>
                <div class="gbs-card__label gbs-lines__label">Líneas</div>
                <div class="gbs-card__chips">
                  <button gbs-message="48" gbs-message-type="img" gbs-click="normal" class="gbs-chip" data-lines="0" hidden><span>224</span><small>líneas</small></button>
                  <button gbs-message="49" gbs-message-type="img" gbs-click="normal" class="gbs-chip" data-lines="1"><span id="lines240">240</span><small>líneas</small></button>
                  <button gbs-message="50" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--dolor" data-lines="2"><span>DOLOR</span><small>infierno PAL</small></button>
                </div>
                <div class="gbs-adj__remind gbs-adj__info gbs-filt__note" id="workLinesNote"></div>
              </div>
              <div id="workNotes"></div>
            </div>

            <div class="gbs-card">
              <div class="gbs-card__label">Modo de salida</div>
              <div class="gbs-grid3" id="workModes">
                <!-- v107o: 720p x3 opens in the shape it was last used in (16:9 the first time); the switch on the mode card changes it -->
                <button gbs-message="53" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-mode="720p x3">720p x3</button>
                <button gbs-message="|" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-mode="720p x4">720p x4</button>
                <button gbs-message="&gt;" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-mode="720p x5"><span>720p x5</span><small>experimental</small></button>
                <button gbs-message="!" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-mode="1080p x4">1080p x4</button>
                <button gbs-message="9" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-mode="1080p x5">1080p x5</button>
              </div>
            </div>

            <div class="gbs-card">
              <div class="gbs-card__label">Consola</div>
              <div class="gbs-seg gbs-cons" id="workConsole">
                <button gbs-message="42" gbs-message-type="img" gbs-click="normal" class="gbs-seg__b" data-prof="auto">Auto</button>
                <button gbs-message="43" gbs-message-type="img" gbs-click="normal" class="gbs-seg__b" data-prof="MD">Mega Drive</button>
                <button gbs-message="44" gbs-message-type="img" gbs-click="normal" class="gbs-seg__b" data-prof="SNES">SNES</button>
                <button gbs-message="45" gbs-message-type="img" gbs-click="normal" class="gbs-seg__b" data-prof="PC-Engine">PC Engine</button>
                <!-- (after v108e) his request: the Dreamcast out of «Otras consolas» now that it is measured -->
                <button gbs-message="69" gbs-message-type="img" gbs-click="normal" class="gbs-seg__b" data-prof="Dreamcast">Dreamcast</button>
              </div>
              <button type="button" class="gbs-cons-more" id="workConsoleMore">Otras consolas ▸</button>
              <div class="gbs-grid3 gbs-cons-others" id="workConsoleOthers" hidden>
                <button gbs-message="60" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="PlayStation">PlayStation</button>
                <button gbs-message="61" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Saturn">Saturn</button>
                <button gbs-message="62" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Nintendo 64">Nintendo 64</button>
                <button gbs-message="63" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="NES">NES</button>
                <button gbs-message="64" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Master System">Master System</button>
                <button gbs-message="65" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Neo Geo AES">Neo Geo AES</button>
                <button gbs-message="66" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Neo Geo CD">Neo Geo CD</button>
                <button gbs-message="67" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="3DO">3DO</button>
                <button gbs-message="68" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Jaguar">Jaguar</button>
                <button gbs-message="70" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="PlayStation 2">PlayStation 2</button>
                <button gbs-message="71" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Xbox">Xbox</button>
                <button gbs-message="72" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="GameCube">GameCube</button>
                <button gbs-message="73" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Wii">Wii</button>
                <!-- (after v108e) his request: for whoever does not see his console and does not know what to choose -->
                <button gbs-message="75" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-cons-chip" data-prof="Others">Otras</button>
              </div>
              <div class="gbs-card__desc gbs-col__desc gbs-cons__desc" id="workConsoleDesc">Elige el sistema que vas a usar para que se muestre lo mejor posible. Actualmente el firmware sólo detecta y está probado con unos pocos.</div>
            </div>
            <div class="gbs-card" id="workSlot">
              <div class="gbs-card__label">Presets personales</div>
              <div class="gbs-card__desc gbs-col__desc">Elige una ranura y guarda en ella lo que tienes ahora o carga lo que guardaste. La ranura elegida es también la que arranca al encender.</div>
              <div class="gbs-presets gbs-slots" gbs-slot-html></div>
              <div class="gbs-grid3">
                <button class="gbs-chip gbs-chip--flat" gbs-element-ref="buttonLoadCustomPreset" gbs-role="preset" onclick="loadPreset()">cargar</button>
                <button class="gbs-chip gbs-chip--save" id="workSlotSave">guardar</button>
                <button class="gbs-chip gbs-chip--flat gbs-danger" onclick="removePreset()">borrar</button>
              </div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button class="gbs-chip gbs-chip--flat" gbs-profile-export>exportar</button>
                <label class="gbs-chip gbs-chip--flat gbs-import">importar<input type="file" class="gbs-profile-input" accept=".gbsp,.json"/></label>
              </div>
              <div class="gbs-card__hint">Exportar guarda la ranura elegida en un archivo .gbsp para compartirlo. Importar lo mete en la ranura elegida si está vacía o en la primera libre: nunca sobrescribe. Para reemplazar un preset, bórralo antes.</div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Resoluciones del GBS-Control original</div>
              <div class="gbs-card__desc gbs-col__desc">Los presets originales de Rama, para pantallas que no acepten los modos de arriba.</div>
              <div class="gbs-grid3">
                <button gbs-message="s" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button1920x1080" gbs-role="preset" class="gbs-chip gbs-chip--flat">1920×1080</button>
                <button gbs-message="p" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button1280x1024" gbs-role="preset" class="gbs-chip gbs-chip--flat">1280×1024</button>
                <button gbs-message="f" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button1280x960" gbs-role="preset" class="gbs-chip gbs-chip--flat">1280×960</button>
                <button gbs-message="g" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button1280x720" gbs-role="preset" class="gbs-chip gbs-chip--flat">1280×720</button>
                <button gbs-message="h" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button720x480" gbs-role="preset" class="gbs-chip gbs-chip--flat">480p / 576p</button>
                <button gbs-message="L" gbs-message-type="user" gbs-click="normal" gbs-element-ref="button15kHzScaleDown" gbs-role="preset" class="gbs-chip gbs-chip--flat">15 kHz</button>
                <button gbs-message="K" gbs-message-type="action" gbs-click="normal" gbs-element-ref="buttonSourcePassThrough" gbs-role="preset" class="gbs-chip gbs-chip--flat">Pass Through</button>
              </div>
              <div class="gbs-sys__sub">
                <button gbs-message="Z" gbs-message-type="action" gbs-click="normal" gbs-toggle-switch="matched" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Preset según la señal</span><small>con el preset por defecto, carga 1280×960 para NTSC 60 Hz y 1280×1024 para PAL 50 Hz</small></span><span class="gbs-sw__knob"></span>
                </button>
                <button gbs-message="v" gbs-message-type="user" gbs-click="normal" gbs-toggle-switch="fullHeight" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Altura completa en 1920×1080</span><small>usa toda la altura de la pantalla en ese preset. Solo funciona con fuentes PAL</small></span><span class="gbs-sw__knob"></span>
                </button>
              </div>
            </div>
          </div>
        </section>

        <section name="ajuste" hidden>
          <div class="gbs-work gbs-adj">
            <div class="gbs-card">
              <div class="gbs-card__label" id="workPlaceLabel">Colocación</div>
              <div class="gbs-card__row">
                <div class="gbs-card__value" id="workMoved">Tal como la midió el firmware</div>
                <div class="gbs-card__tag" id="workSaved" hidden>sin guardar</div>
              </div>
              
            </div>

            <div class="gbs-adj__seg gbs-seg" id="padSeg">
              <button data-mode="move" class="gbs-seg__b" active>Mover</button>
              <button data-mode="crop" class="gbs-seg__b">Bordes</button>
              <button data-mode="scale" class="gbs-seg__b">Escalar</button>
            </div>
            <!-- (after v107s) no "Encoger / Agrandar borde" switch any more: in Bordes every key has two arrows (his design) -->
            <div class="gbs-adj__help" id="padHelp"></div>
            <div class="gbs-adj__pad" id="padKeys">
              <button class="gbs-pad__k gbs-pad__blank"></button>
              <button class="gbs-pad__k" data-pad="up">&#9650;</button>
              <button class="gbs-pad__k gbs-pad__blank"></button>
              <button class="gbs-pad__k" data-pad="left">&#9664;</button>
              <!-- (after v108d) his request: the fine step in the middle of the pad, an arrow-sized key (the mode it was
                   labelled with is already on the Mover / Bordes / Escalar buttons above) -->
              <button class="gbs-pad__k gbs-pad__fine" id="workFine"><span>ajuste<br>fino</span></button>
              <button class="gbs-pad__k" data-pad="right">&#9654;</button>
              <button class="gbs-pad__k gbs-pad__blank"></button>
              <button class="gbs-pad__k" data-pad="down">&#9660;</button>
              <button class="gbs-pad__k gbs-pad__blank"></button>
            </div>
            <div id="padMid" hidden>MOVER</div>
            <!-- (after v107r) three undo buttons, his choice: each one puts back only what it names. The edge one asks first,
                 because it erases the edge kept for this mode. -->
            <div class="gbs-adj__acts gbs-adj__undo">
              <button gbs-message="55" gbs-message-type="img" gbs-click="normal" gbs-confirm="¿Volver a la posición del firmware? Se borrará la posición actual para este modo" class="gbs-bar__ghost" id="workUndoMove"><small>deshacer</small><span>movimiento</span></button>
              <button gbs-message="56" gbs-message-type="img" gbs-click="normal" gbs-confirm="¿Volver al borde del firmware? Se borrará el borde actual para este modo" class="gbs-bar__ghost" id="workUndoEdge"><small>deshacer</small><span>borde</span></button>
              <button gbs-message="57" gbs-message-type="img" gbs-click="normal" class="gbs-bar__ghost" id="workUndoScale"><small>deshacer</small><span>escala</span></button>
            </div>
            <!-- (after v107q) his request: always visible, right under the pad (first moved to the top, then here: whoever plays with the pad sees it) -->
            <div class="gbs-note gbs-adj__edgesaved"><span class="gbs-note__ico">&#9650;</span><span>El borde y la posición quedarán guardados automáticamente para el modo y perfil de esta consola.</span></div>
            <div class="gbs-note gbs-adj__edgesaved"><span class="gbs-note__ico">&#9650;</span><span>Si escalas la imagen, te corresponde guardar a ti los cambios en un preset personal (menú MODO).</span></div>
            <div class="gbs-note gbs-adj__edgesaved"><span class="gbs-note__ico">&#9650;</span><span>Los cambios anteriores pueden ocasionar conflictos y glitches. No es fácil obtener una buena imagen, reajustar la RAM podría ayudar.</span></div>
            <div class="gbs-adj__remind gbs-adj__info">Ajuste fino: mover y escalar avanzan con el paso más pequeño posible. Más toques, pero colocación exacta.</div>
            <div class="gbs-card gbs-adj__mem">
              <div class="gbs-card__label">Memoria <span class="gbs-card__label-note">(sólo si ves glitches)</span></div>
              <div class="gbs-card__desc gbs-col__desc">Si al escalar o posicionar la imagen surgen errores gráficos (glitches, basura o demasiado ruido), a veces se puede corregir modificando la velocidad y/o fase de la RAM.</div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button gbs-message="l" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-mem"><small>velocidad</small><span id="memClock">—</span><small>toca para cambiar</small></button>
                <button gbs-message="37" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-mem"><small>fase</small><span id="memPhase">—</span><small>toca para cambiar</small></button>
              </div>
            </div>
            <div class="gbs-card gbs-adj__quick">
              <div class="gbs-card__label">Bordes y ventana · ajuste rápido</div>
                <div class="gbs-step" data-val="el">
                  <div class="gbs-step__lab"><span>Borde izquierdo</span><b class="gbs-step__val">—</b><small>− menos borde · + más borde</small></div>
                  <button gbs-message="_" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="-" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="er">
                  <div class="gbs-step__lab"><span>Borde derecho</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="~" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="." gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="et">
                  <div class="gbs-step__lab"><span>Borde superior</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="," gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="*" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="eb">
                  <div class="gbs-step__lab"><span>Borde inferior</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="$" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message=";" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-adj__remind gbs-adj__info gbs-filt__note gbs-filt__note--top">Cada borde por separado, sin cambiar de modo en la cruceta. Si ves basura en un lateral, prueba primero a correr la lectura de memoria o el inicio de captura para llevarla fuera de la imagen, y después cierra ese borde. Con «ajuste fino» todos van de uno en uno.</div>
                <div class="gbs-step" data-val="hb">
                  <div class="gbs-step__lab"><span>Lectura de memoria (MEM)</span><b class="gbs-step__val">—</b><small>corre la imagen dentro de su marco, sin mover el marco</small></div>
                  <button gbs-message="-" gbs-message-type="action" gbs-click="repeat" class="gbs-step__b">&#9664;</button>
                  <button gbs-message="+" gbs-message-type="action" gbs-click="repeat" class="gbs-step__b">&#9654;</button>
                </div>
                <div class="gbs-step" data-val="vb">
                  <div class="gbs-step__lab"><span>Memoria vertical (MEM)</span><b class="gbs-step__val">—</b><small>las líneas en blanco de la memoria: si se ven como una raya negra, sácalas de la imagen</small></div>
                  <button gbs-message="51" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#9650;</button>
                  <button gbs-message="52" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#9660;</button>
                </div>
                <div class="gbs-step" data-val="hbin">
                  <div class="gbs-step__lab"><span>Inicio de captura (IF HBIN)</span><b class="gbs-step__val">—</b><small>desde qué punto de cada línea empieza a leer la señal</small></div>
                  <button gbs-message="'" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#9664;</button>
                  <button gbs-message="`" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#9654;</button>
                </div>
            </div>
          </div>
        </section>

        <section name="color" hidden>
          <div class="gbs-work gbs-col">
            <div class="gbs-card">
              <div class="gbs-card__label">Brillo y contraste</div>
                <div class="gbs-step" data-val="br">
                  <div class="gbs-step__lab"><span>Brillo</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="T" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="Z" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="ct">
                  <div class="gbs-step__lab"><span>Contraste</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="M" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="N" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="bl">
                  <div class="gbs-step__lab"><span>Nivel de negro</span><b class="gbs-step__val">—</b><small>después de digitalizar</small></div>
                  <button gbs-message="}" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="{" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="ga">
                  <div class="gbs-step__lab"><span>Ganancia de entrada</span><b class="gbs-step__val">—</b><small>brillo del ADC, por preset</small></div>
                  <button gbs-message="o" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="n" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <button gbs-message="T" gbs-message-type="action" gbs-click="normal" gbs-toggle="adcAutoGain" class="gbs-chip gbs-chip--flat gbs-col__wide">
                  <span>ganancia automática</span><small>calibra unos segundos sobre una pantalla blanca</small>
                </button>
                <!-- (after v111h) his decision of 2026-11-21: the gain his own board finds is kept per console. Simple
                     enough for anyone, so it lives with the picture controls, not in Avanzado. -->
                <button gbs-message="85" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-col__wide">
                  <span>guardar ganancia <b class="gbs-chip__val" id="agSavedNow">—</b></span><small>Cuando la ganancia automática se asiente y la imagen te convenza, guárdala para esta consola. Esto bloquea la ganancia automática hasta que la reinicies.</small>
                </button>
                <button gbs-message="86" gbs-message-type="img" gbs-click="normal" id="agForgetBtn" class="gbs-chip gbs-chip--flat gbs-col__wide" disabled>
                  <span>reiniciar ganancia</span><small>Olvida la ganancia guardada actual. Recomendable al cambiar de cableado o modificar la consola.</small>
                </button>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Color</div>
                <div class="gbs-step" data-val="sa">
                  <div class="gbs-step__lab"><span>Saturación</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="&quot;" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="/" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="hu">
                  <div class="gbs-step__lab"><span>Matiz</span><b class="gbs-step__val">—</b><small>gira el color, 3° por paso</small></div>
                  <button gbs-message="35" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="34" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="su">
                  <div class="gbs-step__lab"><span>Azul</span><b class="gbs-step__val">—</b><small>ganancia Pb/U</small></div>
                  <button gbs-message="H" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="Q" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="sv">
                  <div class="gbs-step__lab"><span>Rojo</span><b class="gbs-step__val">—</b><small>ganancia Pr/V</small></div>
                  <button gbs-message="S" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="P" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <button gbs-message="36" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat gbs-col__wide"><span>matiz a 0</span><small>quita el giro sin tocar la saturación</small></button>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Blanco y tinte</div>
                <div class="gbs-step" data-val="wr">
                  <div class="gbs-step__lab"><span>Blanco R</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="1" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="0" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="wg">
                  <div class="gbs-step__lab"><span>Blanco G</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="3" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="2" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="wb">
                  <div class="gbs-step__lab"><span>Blanco B</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="5" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="4" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="tu">
                  <div class="gbs-step__lab"><span>Tinte azul–amarillo</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="7" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="6" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="tv">
                  <div class="gbs-step__lab"><span>Tinte rojo–cian</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="9" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="8" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Negro de entrada (ADC)</div>
                <div class="gbs-step" data-val="oall">
                  <div class="gbs-step__lab"><span>Negro de entrada</span><b class="gbs-step__val">—</b><small>los tres canales</small></div>
                  <button gbs-message="27" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="26" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="or">
                  <div class="gbs-step__lab"><span>Negro R</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="29" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="28" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="og">
                  <div class="gbs-step__lab"><span>Negro G</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="31" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="30" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-step" data-val="ob">
                  <div class="gbs-step__lab"><span>Negro B</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="33" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="32" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
                <div class="gbs-card__hint">Actúa antes de digitalizar la señal. Úsalo si el negro llega levantado (compuesto a través de un transcodificador): el nivel de negro de arriba no llega ahí. Uno o dos toques ya se notan.</div>
            </div>
            <div class="gbs-card gbs-col__old">
              <div class="gbs-card__label">Calibración del ADC</div>
              <table class="gbs-preferences">
              <tr>
                <td>
                  Calibración del ADC al arrancar
                  <!-- prettier-ignore -->
                  <ul class="gbs-help">
                    <li>GBS-Control calibra los offsets del ADC al arrancar.</li>
                    <li>Si ves los colores desplazados, prueba a desactivarla.</li>
                  </ul>
                </td>
                <td
                  gbs-message="w"
                  gbs-message-type="user"
                  gbs-click="normal"
                  class="gbs-icon"
                  gbs-toggle-switch="enableCalibrationADC"
                >
                  toggle_off
                </td>
              </tr>
              </table>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label gbs-colprof-label" id="colProfLabel">Perfil de color y filtro</div>
              <div class="gbs-card__desc gbs-col__desc" id="colProfDesc">Este firmware recuerda los ajustes de color y filtros para cada consola por separado (sólo si existe su perfil específico, estoy trabajando en ello). Tocando el botón GUARDAR, quedarán almacenados para la próxima vez que la uses, convirtiéndose en su ajuste global.<br><br>Si un juego concreto necesita otros ajustes (ejemplo: típico juego de miedo demasiado oscuro en el que no se ve nada), ¡no guardes aquí! Guarda un preset personal para dicho juego en la pestaña MODO; ese preset llevará sus propios colores y filtros, y no alterará el ajuste global de la consola.</div>
              <!-- v107p: guardar now keeps the filters too (scanlines, line filter, peaking, edge, deinterlacer) and brightness /
                   contrast. "reiniciar" puts the factory values on screen; (after v123d) what this console has saved is kept. -->
              <div class="gbs-grid3">
                <button gbs-message="17" gbs-message-type="img" gbs-click="normal" gbs-confirm="¿Sobrescribir lo guardado de {profile}?" class="gbs-chip gbs-chip--save">guardar</button>
                <button gbs-message="18" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat">cargar</button>
                <button gbs-message="19" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat">reiniciar</button>
              </div>
              <!-- (after v107v) one console's colour and filters to a file and back, to keep a copy or share it -->
              <div class="gbs-grid3 gbs-grid3--2">
                <button class="gbs-chip gbs-chip--flat gbs-col-export">exportar</button>
                <button class="gbs-chip gbs-chip--flat gbs-col-import">importar</button>
              </div>
            </div>
          </div>
        </section>

        <section name="presets" hidden>




          <div class="gbs-work">
            <div class="gbs-card">
              <div class="gbs-card__label">Fuentes VGA de baja resolución</div>
                <button gbs-message="x" gbs-message-type="user" gbs-click="normal" gbs-toggle-switch="preferScalingRgbhv" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Escalar en vez de dejar pasar</span><small>para entradas VGA de PC de 640×480 o menos. Escaladas son más compatibles con las pantallas, pero pueden dar fallos de bordes o de escala. Con consolas no actúa</small></span><span class="gbs-sw__knob"></span>
                </button>
            </div>
          </div>

          <fieldset class="gbs-fieldset">
            <legend class="gbs-fieldset__legend gbs-fieldset__legend--help">
              <div class="gbs-icon">sync_problem</div>
              <div>Sync (fuentes que no enganchan)</div>
            </legend>
            <!-- (after v107r) his description, always visible -->
            <div class="gbs-fieldset__desc">Bloque experimental para reparar la atroz sincronía del transcodificador ODV-II (compuesto a componentes) al usar PC-Engine. No tiene utilidad para el resto de usuarios.</div>
            <!-- prettier-ignore -->
            <ul class="gbs-help">
              <li>Para fuentes con una sincronía débil o problemática (por ejemplo, compuesto convertido a componentes), en las que la imagen salta cada par de segundos.</li>
              <li>Sync tolerante: un corte de menos de unos 0,9 s solo congela la imagen. Se mantiene el preset y se frenan los pasos de recuperación que provocan el salto (subir el nivel SOG, reescribir el coast, congelar y descongelar). Si la señal se ha perdido de verdad, la recuperación normal sigue funcionando. Apagado por defecto; se recuerda.</li>
              <li>SOG +/- cambia el nivel al que se corta la sincronía. Con una punta de sync débil o con ruido, otro nivel suele aguantar donde el actual falla; además repite la búsqueda de fase.</li>
              <li>SyncWatcher para y arranca la vigilancia. Mientras está parado no se detecta nada (un cambio de consola pasará desapercibido), y la barra de estado lo indica con «vigilante parado».</li>
              <li>El auto-lock hace solo lo que harías a mano: para el vigilante en cuanto la imagen se lee estable. El botón recorre los puntos de bloqueo (apagado, 2, 10, 30, 62 lecturas estables): cuanto más tarde, más asentada está la imagen, así que prueba el más alto que se alcance.</li>
              <li>Mientras cuenta, el vigilante no interviene: no hace sus ajustes dinámicos, ni la optimización SOG, ni la búsqueda de fase, porque en una fuente débil eso altera la imagen y devuelve la cuenta a cero. Por eso costaba llegar a los puntos más altos.</li>
              <li>SyncWatcher también sirve para «probar otra vez»: al soltar el vigilante se sueltan con él el coast, la fase y el clamp, y la cuenta vuelve a cero, así que un nuevo intento nunca hereda el estado anterior. Hay una pausa corta antes de que pueda volver a bloquear. Si tras varios intentos sigue sin quedar bien, vuelve a cargar el preset para empezar lo más limpio posible.</li>
              <li>El auto-reintento hace los intentos por ti: suelta el estado bloqueado a los pocos segundos y el siguiente intento llega solo. Mira la imagen y, en cuanto se vea bien, pulsa «capturar sync»: se paran los intentos y ese estado se guarda para el perfil de esta consola.</li>
              <li>Todo lo de este bloque solo actúa sobre un perfil de consola que tenga un estado guardado (o mientras el auto-reintento busca uno), así que una consola que ya funciona se queda exactamente igual.</li>
              <li>Mientras hay un estado guardado en uso, la recuperación ya no sobrescribe el pulse ignore ni el coast, que era lo que lo estropeaba en cada intento.</li>
              <li>El estado guardado se vuelve a escribir tras cada carga de preset, y «cargar sync» lo escribe de nuevo en cualquier momento. Contiene el nivel SOG, el coast y si quedó sin fijar, el pulse ignore y la fase: lo que necesita una fuente débil o problemática y lo que el vigilante tendría que volver a buscar desde cero.</li>
              <li>La receta ODV escribe el estado que se midió funcionando con la PC Engine a través de un transcodificador ODV (nivel SOG 14, coast 16/256 sin fijar, pulse ignore 51), como punto de partida en vez de nada. No es una garantía: captúralo si la imagen sale bien.</li>
              <li>Info de sync escribe el estado actual de la sincronía en el log, para comparar ajustes mientras los pruebas.</li>
            </ul>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button
                gbs-message="23"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">shield</div>
                <div>sync tolerante</div>
              </button>
              <button
                gbs-message="22"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control gbs-button__secondary"
              >
                <div class="gbs-icon">info</div>
                <div>info de sync</div>
              </button>
            </div>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button
                gbs-message="m"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">devices_other</div>
                <div>SyncWatcher</div>
              </button>
              <button
                gbs-message="24"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">lock_clock</div>
                <div>punto de auto-lock</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="20"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">add_circle_outline</div>
                <div>nivel SOG</div>
              </button>
              <button
                gbs-message="21"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">remove_circle_outline</div>
                <div>nivel SOG</div>
              </button>
            </div>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button
                gbs-message="25"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">autorenew</div>
                <div>auto-reintento</div>
              </button>
              <button
                gbs-message="11"
                gbs-message-type="img"
                gbs-confirm="¿Sobrescribir el sync guardado de {profile} con el que tiene ahora el chip?"
                gbs-click="normal"
                class="gbs-button gbs-button__control"
              >
                <div class="gbs-icon">bookmark_add</div>
                <div>capturar sync</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="12"
                gbs-message-type="img"
                gbs-click="normal"
                class="gbs-button gbs-button__control gbs-button__secondary"
              >
                <div class="gbs-icon">bookmark</div>
                <div>cargar sync</div>
              </button>
              <button
                gbs-message="14"
                gbs-message-type="img"
                gbs-confirm="¿Escribir la receta ODV sobre el sync que tiene ahora el chip? El perfil activo es {profile}."
                gbs-click="normal"
                class="gbs-button gbs-button__control gbs-button__secondary"
              >
                <div class="gbs-icon">auto_fix_high</div>
                <div>receta ODV</div>
              </button>
              <button
                gbs-message="13"
                gbs-message-type="img"
                gbs-confirm="¿Borrar el sync guardado de {profile}?"
                gbs-click="normal"
                class="gbs-danger gbs-button gbs-button__control gbs-button__secondary"
              >
                <div class="gbs-icon">bookmark_remove</div>
                <div>olvidar sync</div>
              </button>
            </div>
          </fieldset>
          <fieldset class="gbs-fieldset">
            <legend class="gbs-fieldset__legend">
              <div class="gbs-icon">av_timer</div>
              <div>Frecuencias y FrameTime Lock</div>
            </legend>
            <table class="gbs-preferences">
              <tr>
                <td colspan="2" class="gbs-preferences__child">
                  FrameTime Lock activo
                  <!-- prettier-ignore -->
                  <ul class="gbs-help">
                    <li>Mantiene alineados los tiempos de entrada y salida y corrige la línea de corte horizontal que a veces aparece.</li>
                    <li>Hay dos métodos. Si la pantalla se queda en negro o la imagen se desplaza en vertical, prueba con el otro.</li>
                  </ul>
                </td>
              </tr>
              <tr>
                <td class="gbs-padding__left-16">FrameTime Lock</td>
                <td
                  class="gbs-icon"
                  gbs-message="5"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="frameTimeLock"
                >
                  toggle_off
                </td>
              </tr>
              <tr gbs-ftl-method-rows hidden>
                <td colspan="2" class="gbs-preferences__child">
                  Método de FrameTime Lock
                  <!-- prettier-ignore -->
                  <ul class="gbs-help">
                    <li>VTotal + VSST: ajusta el total vertical y el inicio de la sincronía vertical, para un bloqueo más preciso.</li>
                    <li>Solo VTotal: ajusta únicamente el total vertical; más sencillo.</li>
                  </ul>
                </td>
              </tr>
              <tr gbs-ftl-method-rows hidden>
                <td class="gbs-padding__left-16">VTotal + VSST</td>
                <td
                  class="gbs-icon"
                  gbs-message="j"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="ftlMethodVtotalVsst"
                >
                  toggle_off
                </td>
              </tr>
              <tr gbs-ftl-method-rows hidden>
                <td class="gbs-padding__left-16">VTotal</td>
                <td
                  class="gbs-icon"
                  gbs-message="k"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="ftlMethodVtotalOnly"
                >
                  toggle_off
                </td>
              </tr>
              <tr>
                <td colspan="2" class="gbs-preferences__child">
                  Frecuencia de salida
                  <!-- prettier-ignore -->
                  <ul class="gbs-help">
                    <li>Necesita el generador de reloj externo (Si5351). Sin él, estas opciones no hacen nada.</li>
                    <li>Seguir a la fuente: la salida va exactamente a la frecuencia de la consola (por ejemplo, 59,82 Hz con la PC Engine).</li>
                    <li>59,94 Hz / 60,00 Hz: la salida se queda en una frecuencia estándar (las fuentes de 50 Hz siguen a 50 Hz). Útil con capturadoras que no aceptan frecuencias raras. En estos modos FrameTime Lock no puede seguir a la fuente, así que una línea de corte puede ir desplazándose despacio por la imagen.</li>
                    <li>Exacta: la salida va a la cifra que escribas (por ejemplo 59,8245). Si la consola está a más de 1,5 Hz de esa cifra, la salida la sigue a ella. Como en 59,94 y 60, FrameTime Lock no actúa.</li>
                  </ul>
                </td>
              </tr>
              <tr>
                <td class="gbs-padding__left-16">Seguir a la fuente</td>
                <td
                  class="gbs-icon"
                  gbs-message="b"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="outputRateFollow"
                >
                  toggle_off
                </td>
              </tr>
              <tr>
                <td class="gbs-padding__left-16">59.94 Hz</td>
                <td
                  class="gbs-icon"
                  gbs-message="c"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="outputRate5994"
                >
                  toggle_off
                </td>
              </tr>
              <tr>
                <td class="gbs-padding__left-16">60.00 Hz</td>
                <td
                  class="gbs-icon"
                  gbs-message="d"
                  gbs-message-type="user"
                  gbs-click="normal"
                  gbs-toggle-switch="outputRate6000"
                >
                  toggle_off
                </td>
              </tr>
              <!-- (after v111b) his request: an exact output rate typed in the web -->
              <tr>
                <td class="gbs-padding__left-16">Exacta</td>
                <td
                  class="gbs-icon"
                  gbs-message="81"
                  gbs-message-type="img"
                  gbs-click="normal"
                  gbs-toggle-switch="outputRateExact"
                >
                  toggle_off
                </td>
              </tr>
              <tr>
                <td colspan="2" class="gbs-padding__left-16">
                  <div class="gbs-clk">
                    <input class="gbs-clk__in" id="hzExactIn" type="text" inputmode="decimal" placeholder="59,8245" />
                    <span class="gbs-clk__unit">Hz</span>
                    <button class="gbs-button gbs-clk__btn" data-clk="hz">aplicar</button>
                    <span class="gbs-clk__now" id="hzExactNow">—</span>
                  </div>
                </td>
              </tr>
              <tr>
                <td>
                  Desactivar el generador de reloj externo
                  <!-- prettier-ignore -->
                  <ul class="gbs-help">
                    <li>Por defecto, el generador de reloj externo se activa si está instalado.</li>
                    <li>Puedes desactivarlo si te da problemas con otras opciones. Requiere reiniciar.</li>
                  </ul>
                </td>
                <td
                  gbs-message="X"
                  gbs-message-type="user"
                  gbs-click="normal"
                  class="gbs-icon"
                  gbs-toggle-switch="disableExternalClockGenerator"
                >
                  toggle_off
                </td>
              </tr>
            </table>
          </fieldset>
        


          <!-- <fieldset class="gbs-fieldset controls-desktop">
            <legend class="gbs-fieldset__legend">
              <div class="gbs-icon">control_camera</div>
              <div>Picture Control</div>
            </legend>
            <div class="">
              <button active class="gbs-button direction">
                <div class="gbs-icon">open_with</div>
                <div>move</div>
              </button>
              <div class="keyboard">
                <div>
                  <button
                    gbs-message="7"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_left
                  </button>
                  <button
                    gbs-message="*"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_up
                  </button>
                  <button
                    gbs-message="6"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_right
                  </button>
                </div>

                <div class="gbs-margin__bottom--16">
                  <button class="gbs-button gbs-icon" disabled>
                    south_west
                  </button>
                  <button
                    gbs-message="/"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_down
                  </button>
                  <button class="gbs-button gbs-icon" disabled>
                    south_east
                  </button>
                </div>
              </div>
            </div>
            <div class="">
              <button class="gbs-button direction" active>
                <div class="gbs-icon">zoom_out_map</div>
                <div>scale</div>
              </button>
              <div class="keyboard">
                <div>
                  <button
                    gbs-message="h"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_left
                  </button>
                  <button
                    gbs-message="4"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_up
                  </button>
                  <button
                    gbs-message="z"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_right
                  </button>
                </div>

                <div class="gbs-margin__bottom--16">
                  <button class="gbs-button gbs-icon" disabled>
                    south_west
                  </button>
                  <button
                    gbs-message="5"
                    gbs-message-type="action"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_down
                  </button>
                  <button class="gbs-button gbs-icon" disabled>
                    south_east
                  </button>
                </div>
              </div>
            </div>
            <div class="">
              <button class="gbs-button direction" active>
                <div class="gbs-icon">crop_free</div>
                <div>borders</div>
              </button>
              <div class="keyboard">
                <div>
                  <button
                    gbs-message="B"
                    gbs-message-type="user"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_left
                  </button>
                  <button
                    gbs-message="C"
                    gbs-message-type="user"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_up
                  </button>
                  <button
                    gbs-message="A"
                    gbs-message-type="user"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_right
                  </button>
                </div>

                <div class="gbs-margin__bottom--16">
                  <button class="gbs-button gbs-icon" disabled>
                    south_west
                  </button>
                  <button
                    gbs-message="D"
                    gbs-message-type="user"
                    gbs-click="repeat"
                    class="gbs-button gbs-icon gbs-button__secondary"
                  >
                    keyboard_arrow_down
                  </button>
                  <button class="gbs-button gbs-icon" disabled>
                    south_east
                  </button>
                </div>
              </div>
            </div>
          </fieldset> -->
                  <div gbs-section="developer" class="gbs-avz__dev" hidden>

          <fieldset class="gbs-fieldset">
            <legend class="gbs-fieldset__legend">
              <div class="gbs-icon">input</div>
              <div>Developer</div>
            </legend>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button class="gbs-button" gbs-output-toggle>
                <div class="gbs-icon">code</div>
                <div>Toggle Console</div>
              </button>
            </div>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button
                gbs-message="-"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">keyboard_arrow_left</div>
                <div>MEM Left</div>
              </button>
              <button
                gbs-message="+"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">keyboard_arrow_right</div>
                <div>MEM Right</div>
              </button>
              <button
                gbs-message="1"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">keyboard_arrow_left</div>
                <div>HS Left</div>
              </button>
              <button
                gbs-message="0"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">keyboard_arrow_right</div>
                <div>HS Right</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="e"
                gbs-message-type="user"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">list</div>
                <div>List Options</div>
              </button>
              <button
                gbs-message="i"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">info</div>
                <div>Print Info</div>
              </button>
              <button
                gbs-message=","
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">alarm</div>
                <div>Get Video Timings</div>
              </button>
            </div>

            <div class="gbs-flex">
              <button
                gbs-message="F"
                gbs-message-type="user"
                gbs-click="normal"
                class="gbs-button gbs-margin__bottom--16"
              >
                <div class="gbs-icon">add_a_photo</div>
                <div>Freeze Capture</div>
              </button>
            </div>

            <div class="gbs-flex">
              <button
                gbs-message="F"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">wb_sunny</div>
                <div>ADC Filter</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="D"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">bug_report</div>
                <div>Debug View</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="a"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">add_circle_outline</div>
                <div>HTotal++</div>
              </button>
              <button
                gbs-message="A"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">remove_circle_outline</div>
                <div>HTotal--</div>
              </button>
              <button
                gbs-message="."
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">sync_problem</div>
                <div>Resync HTotal</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="n"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">calculate</div>
                <div>PLL divider++</div>
              </button>
              <button
                gbs-message="8"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">invert_colors</div>
                <div>Invert Sync</div>
              </button>
            </div>
            <div class="gbs-flex">
              <!-- (after v107r) SyncWatcher back in Developer, next to SyncProcessor, as it used to be (the one in Sync stays) -->
              <button
                gbs-message="m"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">devices_other</div>
                <div>SyncWatcher</div>
              </button>
              <button
                gbs-message="l"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">settings_backup_restore</div>
                <div>SyncProcessor</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="o"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">insights</div>
                <div>Oversampling</div>
              </button>
              <button
                gbs-message="S"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">settings_input_hdmi</div>
                <div>60/50Hz HDMI</div>
              </button>

              <button
                gbs-message="E"
                gbs-message-type="user"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">bug_report</div>
                <div>IF Auto Offset</div>
              </button>
            </div>
            <div class="gbs-flex">
              <button
                gbs-message="z"
                gbs-message-type="user"
                gbs-click="normal"
                class="gbs-button"
              >
                <div class="gbs-icon">format_align_justify</div>
                <div>SOG Level--</div>
              </button>

              <button
                gbs-message="q"
                gbs-message-type="action"
                gbs-click="normal"
                class="gbs-button gbs-button__secondary"
              >
                <div class="gbs-icon">model_training</div>
                <div>Reset Chip</div>
              </button>
            </div>
            <!-- (after v111b) his request: the clocks, typed or nudged live -->
            <div class="gbs-regtest">
              <div class="gbs-regtest__title">Relojes</div>
              <ul class="gbs-help">
                <li>Reloj del ADC (PLLAD_MD): cuántas muestras toma el chip por línea de la consola. Es el «PLL divider++» de arriba, pero con la cifra exacta. Cambia en vivo; al cargar un preset vuelve a su valor. Ojo: ciertos valores cortan el wifi (me pasó con 1784 en la Mega Drive PAL).</li>
                <li>Reloj de salida (Si5351): el que marca los Hz de salida. Útil para mirar y probar; el ajuste de frecuencia de salida y FrameTime Lock lo pueden volver a mover.</li>
                <li>Línea de corte: «fase FTL» mueve el punto en el que FrameTime Lock coloca la salida respecto a la entrada (de fábrica, 90°), y con él la línea de corte. «Reiniciar memoria» vuelve a arrancar la memoria de imagen sin recargar el preset. Ninguno se guarda.</li>
              </ul>
              <div class="gbs-clk">
                <span class="gbs-clk__lab">ADC</span>
                <span class="gbs-clk__now" id="adcNow">—</span>
                <button class="gbs-button gbs-clk__btn" data-clk="adc-">−</button>
                <button class="gbs-button gbs-clk__btn" data-clk="adc+">+</button>
                <input class="gbs-clk__in" id="adcIn" type="text" inputmode="numeric" placeholder="1759" />
                <button class="gbs-button gbs-clk__btn" data-clk="adc">aplicar</button>
              </div>
              <div class="gbs-clk">
                <span class="gbs-clk__lab">Salida</span>
                <span class="gbs-clk__now" id="siNow">—</span>
                <button class="gbs-button gbs-clk__btn" data-clk="si-">−1 kHz</button>
                <button class="gbs-button gbs-clk__btn" data-clk="si+">+1 kHz</button>
                <input class="gbs-clk__in" id="siIn" type="text" inputmode="decimal" placeholder="81,000" />
                <span class="gbs-clk__unit">MHz</span>
                <button class="gbs-button gbs-clk__btn" data-clk="si">aplicar</button>
              </div>
              <div class="gbs-clk">
                <span class="gbs-clk__lab">Fase FTL</span>
                <span class="gbs-clk__now" id="ftlPhaseNow">—</span>
                <button gbs-message="83" gbs-message-type="img" gbs-click="normal" class="gbs-button gbs-clk__btn">−15°</button>
                <button gbs-message="84" gbs-message-type="img" gbs-click="normal" class="gbs-button gbs-clk__btn">+15°</button>
                <button gbs-message="82" gbs-message-type="img" gbs-click="normal" class="gbs-button gbs-clk__btn">Reiniciar memoria</button>
                <!-- (after v120c) his request of 2026-11-21: a way back to the values the firmware carries -->
                <button gbs-message="87" gbs-message-type="img" gbs-click="normal" class="gbs-button gbs-clk__btn">Memoria de fábrica</button>
              </div>
            </div>
            <!-- (after v111) his request: try the chip registers nobody used yet (deinterlacer and output fields) -->
            <div class="gbs-regtest">
              <div class="gbs-regtest__title">Registros por probar</div>
              <ul class="gbs-help">
                <li>Registros del chip que el firmware no usaba: los del desentrelazador (MADPT) y uno de los campos de salida (VDS_HALF_EN). Prueba con una fuente entrelazada (480i / 576i).</li>
                <li>«Siguiente» elige el registro; «−» y «+» cambian su valor. Nada se guarda: al cargar un preset vuelven a su valor.</li>
              </ul>
              <div class="gbs-regtest__state" id="regTestState">—</div>
              <div class="gbs-flex">
                <button gbs-message="77" gbs-message-type="img" gbs-click="normal" class="gbs-button"><div class="gbs-icon">skip_next</div><div>Siguiente</div></button>
                <button gbs-message="79" gbs-message-type="img" gbs-click="normal" class="gbs-button"><div class="gbs-icon">remove</div><div>−</div></button>
                <button gbs-message="78" gbs-message-type="img" gbs-click="normal" class="gbs-button"><div class="gbs-icon">add</div><div>+</div></button>
                <button gbs-message="80" gbs-message-type="img" gbs-click="normal" class="gbs-button gbs-button__secondary"><div class="gbs-icon">undo</div><div>Deshacer todo</div></button>
              </div>
            </div>
          </fieldset>
                  </div>
        </section>


        <section name="filters" hidden>
          <div class="gbs-work gbs-filt">
            <div class="gbs-card">
              <div class="gbs-card__label">Scanlines</div>
                <div class="gbs-adj__remind gbs-adj__info gbs-filt__note" id="slDolor" hidden>Scanlines no disponibles en este modo.</div>
                <button gbs-message="7" gbs-message-type="user" gbs-click="normal" gbs-toggle="scanlines" class="gbs-sw" id="slSwitch">
                  <span class="gbs-sw__lab"><span>Scanlines</span><small>solo con fuentes 240p o con 480i en Bob</small></span><span class="gbs-sw__knob"></span>
                </button>
                <div class="gbs-step" id="slRow">
                  <div class="gbs-step__lab"><span>Intensidad</span><b class="gbs-step__val" id="slVal">—</b><small>0 es el valor mínimo, no las apaga</small></div>
                  <button gbs-message="G" gbs-message-type="user" gbs-click="normal" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="K" gbs-message-type="user" gbs-click="normal" class="gbs-step__b">+</button>
                </div>
                <button gbs-message="m" gbs-message-type="user" gbs-click="normal" gbs-toggle="vdsLineFilter" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Filtro de línea</span><small>Interpolación vertical: mezcla cada línea con la de al lado para que todas salgan del mismo grosor. En los modos x5 se activa solo para que las scanlines encajen en su sitio.</small></span><span class="gbs-sw__knob"></span>
                </button>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Nitidez</div>
                <button gbs-message="f" gbs-message-type="action" gbs-click="normal" gbs-toggle="peaking" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Peaking</span><small>más contraste en los saltos de brillo horizontales</small></span><span class="gbs-sw__knob"></span>
                </button>
                <div class="gbs-step" data-val="pf">
                  <div class="gbs-step__lab"><span>Nivel de peaking</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="?" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="\" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
              <div class="gbs-adj__remind gbs-adj__info gbs-filt__note" id="pkNote">Al aumentar la nitidez de forma artificial generamos halos blancos y el ruido será más visible. Señales borrosas a veces se benefician, aunque dependerá de tu gusto. Prueba y decide.</div>
                <button gbs-message="V" gbs-message-type="action" gbs-click="normal" gbs-toggle="step" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Respuesta de borde</span><small>bordes de color horizontales más definidos</small></span><span class="gbs-sw__knob"></span>
                </button>
                <div class="gbs-step" data-val="sg">
                  <div class="gbs-step__lab"><span>Nivel de borde</span><b class="gbs-step__val">—</b></div>
                  <button gbs-message="41" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="40" gbs-message-type="img" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
              <div class="gbs-adj__remind gbs-adj__info gbs-filt__note">Afila los cambios de color: el color se procesa con menos detalle que el brillo y en los bordes tiende a correrse. Más nivel deja las transiciones más limpias, pero si te pasas aparecen contornos de color junto a los bordes. Prueba y decide.</div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Desentrelazado</div>
              <div class="gbs-seg gbs-filt__seg">
                <button gbs-message="r" gbs-message-type="user" gbs-click="normal" gbs-toggle-switch="bob" class="gbs-seg__b">Motion Adaptive</button>
                <button gbs-message="q" gbs-message-type="user" gbs-click="normal" gbs-toggle-switch="motionAdaptive" class="gbs-seg__b">Bob</button>
              </div>
              <div class="gbs-card__hint">Solo actúa con fuentes entrelazadas (480i / 576i). Motion Adaptive quita el parpadeo, con algún defecto en lo que se mueve. Bob no añade retardo y se puede combinar con scanlines, pero parpadea.</div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Ruido</div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button gbs-message="W" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat" data-val="nf"><span>filtro de ruido ▸</span><small>siguiente nivel</small></button>
              </div>
              <div class="gbs-adj__remind gbs-adj__info gbs-filt__note gbs-filt__note--top">Limpia el ruido de la señal antes de digitalizarla. Probablemente imperceptible con fuentes de 240p. No comprobado con VGA y superiores.</div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label">Muestreo del ADC</div>
                <div class="gbs-step" data-val="ph">
                  <div class="gbs-step__lab"><span>Fase</span><b class="gbs-step__val">—</b><small>el momento en que se lee cada píxel</small></div>
                  <button gbs-message="Y" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">&#8722;</button>
                  <button gbs-message="y" gbs-message-type="user" gbs-click="repeat" class="gbs-step__b">+</button>
                </div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button gbs-message="I" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat"><span>fase automática</span></button>
              </div>
              <div class="gbs-card__hint">Recorre los valores sobre una imagen con detalle y quédate con la más nítida y que menos tiemble.</div>
            </div>
            <div class="gbs-card">
              <div class="gbs-card__label gbs-colprof-label">Perfil de color y filtro</div>
              <div class="gbs-card__desc gbs-col__desc">Este firmware recuerda los ajustes de color y filtros para cada consola por separado (sólo si existe su perfil específico, estoy trabajando en ello). Tocando el botón GUARDAR, quedarán almacenados para la próxima vez que la uses, convirtiéndose en su ajuste global.<br><br>Si un juego concreto necesita otros ajustes (ejemplo: típico juego de miedo demasiado oscuro en el que no se ve nada), ¡no guardes aquí! Guarda un preset personal para dicho juego en la pestaña MODO; ese preset llevará sus propios colores y filtros, y no alterará el ajuste global de la consola.</div>
              <!-- (after v107q) a replica of the colour profile card of Color, his request: the same buttons, the same messages, so the
                   changes made in Filtros can be saved without going back to the other tab.
                   v107p: guardar now keeps the filters too (scanlines, line filter, peaking, edge, deinterlacer) and brightness /
                   contrast. "reiniciar" puts the factory values on screen; (after v123d) what this console has saved is kept. -->
              <div class="gbs-grid3">
                <button gbs-message="17" gbs-message-type="img" gbs-click="normal" gbs-confirm="¿Sobrescribir lo guardado de {profile}?" class="gbs-chip gbs-chip--save">guardar</button>
                <button gbs-message="18" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat">cargar</button>
                <button gbs-message="19" gbs-message-type="img" gbs-click="normal" class="gbs-chip gbs-chip--flat">reiniciar</button>
              </div>
              <!-- (after v107v) one console's colour and filters to a file and back, to keep a copy or share it -->
              <div class="gbs-grid3 gbs-grid3--2">
                <button class="gbs-chip gbs-chip--flat gbs-col-export">exportar</button>
                <button class="gbs-chip gbs-chip--flat gbs-col-import">importar</button>
              </div>
            </div>
          </div>
        </section>



        <section name="system" hidden>
          <div class="gbs-work gbs-sys">
            <div class="gbs-card">
              <div class="gbs-card__label">Wi-Fi</div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button class="gbs-chip gbs-chip--flat" gbs-wifi-ap><span>Punto de acceso</span><small>el GBS crea su propia red</small></button>
                <button class="gbs-chip gbs-chip--flat" gbs-wifi-station><span gbs-wifi-station-ssid>Tu red</span><small>toca para buscar redes</small></button>
              </div>
              <div class="gbs-sys__sub" gbs-wifi-list hidden>
                <div class="gbs-card__label">Elige una red</div>
                <table class="gbs-wifi__list"></table>
              </div>
              <div class="gbs-sys__sub gsb-wifi__connect" hidden>
                <div class="gbs-card__label">Conectar</div>
                <input class="gbs-sys__input gbs-wifi__input" placeholder="Red" type="text" readonly gbs-input="ssid" />
                <input class="gbs-sys__input gbs-wifi__input" placeholder="Contraseña" type="password" gbs-input="password" />
                <button gbs-wifi-connect-button class="gbs-chip gbs-chip--save gbs-sys__wide">conectar</button>
              </div>
            </div>

            <div class="gbs-card">
              <div class="gbs-card__label">Actualizar firmware</div>
              <div class="gbs-card__desc gbs-col__desc">Elige el archivo firmware.bin del proyecto compilado (.pio/build/gbsc/firmware.bin) y pulsa subir. No apagues el GBS hasta que termine.</div>
              <div class="gbs-sys__ver">Versión de la web: <b>3x4x-v123l</b></div>
              <label class="gbs-chip gbs-chip--flat gbs-sys__file"><span>elegir archivo</span><small gbs-update-filename>ninguno elegido</small><input type="file" accept=".bin" gbs-update-file /></label>
              <div class="gbs-sys__status" gbs-update-status></div>
              <div class="gbs-sys__bar" gbs-update-bar-wrap hidden><div gbs-update-bar></div></div>
              <button gbs-update-upload class="gbs-chip gbs-chip--save gbs-sys__wide">subir y actualizar</button>
            </div>

            <div class="gbs-card">
              <div class="gbs-card__label">Copia de seguridad</div>
              <div class="gbs-card__desc gbs-col__desc">Guarda en un único archivo todos los ajustes y presets del GBS-C, o recupéralos desde un archivo previo de respaldo.<br><br>La copia sólo sirve para este mismo GBS.<br><br>Si sólo quieres exportar/importar/compartir presets de uno en uno, hazlo desde la pestaña MODO.</div>
              <div class="gbs-grid3 gbs-grid3--2">
                <button class="gbs-chip gbs-chip--flat gbs-backup-button"><span>guardar copia</span><small gbs-progress gbs-progress-backup></small></button>
                <button class="gbs-chip gbs-chip--flat gbs-sys__restore gbs-danger"><span>restaurar</span><small gbs-progress gbs-progress-restore></small><input type="file" class="gbs-backup-input" accept=".bin" /></button>
              </div>
            </div>

            <div class="gbs-card">
              <div class="gbs-card__label">Opciones del GBS</div>
                <button gbs-message="R" gbs-message-type="user" gbs-click="normal" gbs-toggle-switch="reverseRotaryEncoderForOledMenu" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Invertir el control del menú OLED</span><small>algunos GBS (como el ODV-GBS-C) lo necesitan para que girar a la derecha baje en el menú</small></span><span class="gbs-sw__knob"></span>
                </button>
                <button gbs-message="L" gbs-message-type="action" gbs-click="normal" gbs-toggle-switch="wantOutputComponent" class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Salida por componente (YPbPr)</span><small>apagado: RGBHV, para cables VGA o conversores a HDMI. Encendido: componente, experimental y poco compatible. Afecta a todos los modos</small></span><span class="gbs-sw__knob"></span>
                </button>
                <button gbs-dev-switch class="gbs-sw">
                  <span class="gbs-sw__lab"><span>Modo desarrollador</span><small>muestra el bloque Developer en Avanzado, con herramientas de diagnóstico</small></span><span class="gbs-icon gbs-sw__icon">toggle_off</span><span class="gbs-sw__knob"></span>
                </button>
            </div>

            <div class="gbs-card gbs-sys__danger">
              <div class="gbs-card__label">Zona delicada</div>
              <div class="gbs-grid3">
                <button gbs-message="a" gbs-message-type="user" gbs-click="normal" class="gbs-chip gbs-chip--flat">reiniciar</button>
                <button gbs-message="c" gbs-message-type="action" gbs-click="normal" class="gbs-chip gbs-chip--flat">activar OTA</button>
                <!-- (after v108f) his request: it erases everything the user did; the personal presets only after a second yes -->
                <button id="sysFactory" class="gbs-chip gbs-chip--flat gbs-sys__reset gbs-danger">valores de fábrica</button>
              </div>
              <div class="gbs-card__hint">OTA permite actualizar por red desde el ordenador (PlatformIO). Se desactiva al reiniciar.</div>
            </div>
          </div>
        </section>
        <section name="prompt" class="gbs-modal" hidden>
          <div class="gbs-card gbs-modal__card">
            <div class="gbs-card__label" gbs-prompt-content>Nombre</div>
            <input class="gbs-sys__input" type="text" gbs-input="prompt-input" maxlength="25" />
            <div class="gbs-grid3 gbs-grid3--2">
              <button gbs-prompt-cancel class="gbs-chip gbs-chip--flat">cancelar</button>
              <button gbs-prompt-ok class="gbs-chip gbs-chip--save">aceptar</button>
            </div>
          </div>
        </section>
        <section name="confirm" class="gbs-modal" hidden>
          <div class="gbs-card gbs-modal__card">
            <div class="gbs-card__label">Confirmar</div>
            <div class="gbs-modal__text" gbs-confirm-content></div>
            <div class="gbs-grid3 gbs-grid3--2">
              <button gbs-confirm-cancel class="gbs-chip gbs-chip--flat">cancelar</button>
              <button gbs-confirm-ok class="gbs-chip gbs-chip--save">aceptar</button>
            </div>
          </div>
        </section>
        <section name="alert" class="gbs-modal" hidden>
          <div class="gbs-card gbs-modal__card">
            <div class="gbs-card__label">Aviso</div>
            <div class="gbs-modal__text" gbs-alert-content></div>
            <div class="gbs-grid3 gbs-grid3--2">
              <span></span>
              <button gbs-alert-ok class="gbs-chip gbs-chip--save">aceptar</button>
            </div>
          </div>
        </section>
        <div class="gbs-output">
          <fieldset class="gbs-fieldset gbs-fieldset-output">
            <legend class="gbs-fieldset__legend">
              <div class="gbs-icon">code</div>
              <div>Output</div>
            </legend>
            <div class="gbs-flex gbs-margin__bottom--16" gbs-output-clear>
              <button class="gbs-button gbs-icon">delete_outline</button>
            </div>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button class="gbs-button" gbs-log-mode="[">Log: clean</button>
              <button class="gbs-button" gbs-log-mode="]">Log: full</button>
            </div>
            <div class="gbs-flex gbs-margin__bottom--16">
              <button class="gbs-button" gbs-diag-copy>Copy diagnostics</button>
              <!-- v107o: the console fingerprint, copied the same way -->
              <button class="gbs-button" gbs-diag-copy="54">Copiar huella</button>
            </div>
            <div class="gbs-flex">
              <textarea
                id="outputTextArea"
                class="gbs-output__textarea"
              ></textarea>
            </div>
          </fieldset>
        </div>
      </div>
      <div class="gbs-loader"><img /></div>
    </div>
    <div class="gbs-wifi-warning" id="websocketWarning">
      <div class="gbs-icon blink_me">signal_wifi_off</div>
    </div>
    <script>
      ${js}
    </script>
  </body>
</html>
