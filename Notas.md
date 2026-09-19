# Vita-DSDA-Doom - Notas de port

## Estado de referencia

Estas notas se prepararon sobre el fork TonyFideo/Vita-DSDA-Doom, rama master, partiendo del commit 08d0369c3e4c491adea755ee05c5c7106d45a558 de DSDA-Doom.

Version de DSDA observada en la base actual: 0.29.4.

Objetivo inicial:

- portar DSDA-Doom a PS Vita;
- usar exclusivamente el renderer SOFTWARE de DSDA;
- usar VitaGL solamente como presentador del framebuffer software en pantalla;
- arrancar directamente el juego, sin launcher;
- buscar automaticamente el IWAD en ux0, luego uma0 y luego ur0;
- usar 960x544 como resolucion interna software inicial;
- dejar desde el comienzo una separacion limpia entre resolucion interna y resolucion fisica para que un launcher futuro pueda bajar la resolucion;
- conservar la arquitectura de DSDA para que en el futuro se pueda portar su renderer OpenGL y elegir SOFTWARE u OpenGL antes de arrancar.

No se debe confundir renderer de DSDA con backend de presentacion:

~~~text
DSDA renderer SOFTWARE
        |
        v
framebuffer indexado de 8 bits
        |
        v
conversion por paleta a RGBA
        |
        v
textura VitaGL
        |
        v
pantalla 960x544
~~~

En esta fase V_IsSoftwareMode() debe seguir siendo verdadero. El hecho de usar VitaGL para presentar la textura NO debe convertir el juego a VID_MODEGL.

---

## 1. Situacion de las dependencias

### Dependencias requeridas actualmente por DSDA

El CMake actual de DSDA exige:

- SDL2
- SDL2_mixer
- libsndfile
- zlib
- libzip
- OpenGL
- GLU

Las cinco primeras existen en VitaSDK. OpenGL/GLU de escritorio son el principal problema del build actual y deben separarse del renderer software.

Estado observado en VitaSDK:

| Dependencia | Estado Vita | Notas |
|---|---|---|
| SDL2 | Disponible | Paquete actual observado: 2.32.8 |
| SDL2_mixer | Disponible | Paquete actual observado: 2.8.2 |
| libsndfile | Disponible | Paquete actual observado: 1.2.2 |
| zlib | Disponible | Sin necesidad de port propio |
| libzip | Disponible | Paquete actual observado: 1.11.4 |
| SDL2_image | Disponible | Opcional |
| libxmp | Disponible | Opcional |
| libmad | Disponible | Opcional |
| libvorbis / vorbisfile | Disponible | Opcional |
| FluidSynth | Parcial | VitaSDK ofrece fluidsynth-lite, no es drop-in completo para DSDA actual |
| PortMidi | No localizado | Desactivar inicialmente |
| vitaGL | Disponible | El paquete normal no activa ENABLE_LEGACY_PIPELINE |
| vitaShaRK | Disponible | Dependencia de VitaGL moderno |
| SceShaccCgExt | Disponible | Dependencia de VitaGL/vitaShaRK |
| taihen | Disponible | Dependencia de VitaGL/vitaShaRK |
| libmathneon | Disponible | Dependencia de VitaGL |

### Paquetes recomendados para el primer build

~~~text
sdl2
sdl2_mixer
libsndfile
zlib
libzip
vitaShaRK
SceShaccCgExt
taihen
libmathneon
~~~

Para VitaGL conviene usar un build local/reproducible del source de VitaGL en lugar de depender solamente del binario precompilado del paquete.

Motivo: el VITABUILD actual de VitaSDK compila VitaGL con NO_DEBUG=1, pero no activa ENABLE_LEGACY_PIPELINE=1. El port futuro del renderer OpenGL de DSDA y un presentador sencillo basado en pipeline legacy pueden necesitar ese soporte.

Configuracion recomendada para la copia local de VitaGL:

~~~text
ENABLE_LEGACY_PIPELINE=1
NO_SPLASHSCREEN=1
~~~

Durante desarrollo pueden activarse tambien LOG_ERRORS y HAVE_PROFILING. Para release puede usarse NO_DEBUG=1 una vez estable.

---

## 2. El renderer software de DSDA ya es portable

DSDA conserva el renderer clasico por CPU.

R_RenderPlayerView mantiene dos rutas claramente diferenciadas:

- software: R_DrawPlanes, R_DrawMasked y las funciones clasicas de columnas/spans;
- OpenGL: construccion y envio de escena mediante gld_*.

Por tanto no hay que reescribir su renderer para Vita.

El flujo deseado queda:

~~~text
R_RenderBSPNodes
R_DrawPlanes
R_DrawMasked
HUD / automap / menus
        |
        v
screens[0]
        |
        v
I_FinishUpdate
        |
        v
VitaGL presenter
~~~

Esto evita el problema de pisos/techos del renderer OpenGL durante la primera etapa. En software, flats y visplanes siguen siendo dibujados por CPU.

---

## 3. El principal bloqueo de compilacion no es ARM: es OpenGL obligatorio

Actualmente prboom2/cmake/DsdaDependencies.cmake hace OpenGL obligatorio y prboom2/src/CMakeLists.txt agrega siempre los archivos gl_* al ejecutable.

Para Vita SOFTWARE debe existir una opcion real de build, por ejemplo:

~~~text
DSDA_ENABLE_OPENGL_RENDERER=OFF
DSDA_VITA_PRESENT_VITAGL=ON
~~~

La primera controla el renderer OpenGL de DSDA.

La segunda controla unicamente la presentacion del framebuffer software en PS Vita.

Al desactivar DSDA_ENABLE_OPENGL_RENDERER se deben eliminar del target:

- gl_clipper.c
- gl_drawinfo.c
- gl_fbo.c
- gl_light.c
- gl_main.c
- gl_map.c
- gl_missingtexture.c
- gl_opengl.c
- gl_preprocess.c
- gl_progress.c
- gl_shader.c
- gl_sky.c
- gl_texture.c
- gl_vertex.c
- gl_wipe.c
- dsda/gl/render_scale.c cuando dependa de la ruta GL

Tambien debe dejar de ejecutarse find_package(OpenGL 2.0 REQUIRED) y dejar de enlazarse OpenGL::GL / OpenGL::GLU cuando esa opcion este desactivada.

Hay referencias gld_* fuera de los archivos gl_*. Entre los archivos a revisar estan:

- v_video.c
- f_wipe.c
- r_bsp.c
- r_main.c
- r_segs.c
- r_things.c
- r_fps.c
- d_main.c
- am_map.c
- p_setup.c
- p_floor.c
- p_ceilng.c
- g_game.c
- m_menu.c
- dsda/settings.c
- dsda/configuration.c
- dsda/skip.c
- SDL/i_video.c
- SDL/i_sshot.c
- e6y.c
- i_video.h
- textscreen/txt_sdl.c

No basta con que V_IsOpenGLMode() sea falso en runtime: el linker seguira exigiendo los simbolos gld_* si las llamadas quedan compiladas.

Solucion recomendada a largo plazo:

1. definir DSDA_ENABLE_OPENGL_RENDERER en CMake;
2. proteger includes SDL_opengl / gl_struct / gl_intern cuando corresponda;
3. proteger llamadas gld_* en codigo comun;
4. mantener VID_MODEGL y la arquitectura de V_InitMode, pero no compilar el case GL completo hasta que el renderer este portado;
5. en Vita inicial, I_DesiredVideoMode debe forzar VID_MODESW.

Esto deja preparado el futuro launcher sin mezclar el presentador VitaGL con el renderer GL de DSDA.

---

## 4. Portabilidad ARM

### Situacion general

No se encontro una dependencia general de:

- SSE;
- MMX;
- NEON;
- codigo exclusivo x86/x86_64.

El renderer software usa principalmente C, C99, algo de C++11 y aritmetica fixed-point 16.16.

FixedMul usa int64_t y es portable a ARMv7.

Existe un resto historico de asm x86 en r_things.c para copiar arrays de punteros, pero tiene fallback por memcpy para otras arquitecturas.

La genealogia de PrBoom/DSDA ya contiene correcciones historicas especificas para packing en ARM. Por tanto no hay que crear un renderer ARM nuevo.

### Riesgo real: alineacion

DSDA retiro -Wcast-align en 2023 porque existe codigo que rompe supuestos de alineacion.

Ejemplos actuales:

~~~c
width = *((const int16_t *) lump);
offset = *((const int32_t *) p);
~~~

Aparecen al menos en:

- dsda/cr_table.c
- v_video.c

En ARMv7 conviene sustituir lecturas potencialmente desalineadas por helpers basados en memcpy y LittleShort/LittleLong.

Ejemplo conceptual:

~~~c
static uint32_t ReadLE32Unaligned(const void *ptr)
{
    uint32_t value;
    memcpy(&value, ptr, sizeof(value));
    return LittleLong(value);
}
~~~

Tambien debe revisarse g_overflow.c, donde se hacen casts de memoria a unsigned short* / unsigned int*.

No hay que reescribir todo el motor antes de probar. La estrategia es:

1. corregir los accesos claramente desalineados en rutas de carga/render;
2. compilar con warnings de alineacion para una build de auditoria;
3. probar en Vita/Vita3K;
4. corregir el resto segun aparezca.

---

## 5. Toolchain ARM de VitaSDK

El toolchain oficial de VitaSDK define:

~~~text
CMAKE_SYSTEM_PROCESSOR = armv7-a
C compiler = arm-vita-eabi-gcc
C++ compiler = arm-vita-eabi-g++
~~~

DSDA ya exige C++11 para algunas unidades y VitaSDK lo soporta.

Flags iniciales razonables para Vita:

~~~text
-O3
-mtune=cortex-a9
-mfpu=neon
-fsigned-char
~~~

No se recomienda introducir globalmente -fno-short-enums sin una razon demostrada. En ports anteriores de PrBoom esto produjo problemas ABI con bibliotecas como libmad.

NEON no es necesario para lograr el primer port funcional. Se debe optimizar despues de perfilar.

---

## 6. Filesystem y estructura de datos

### Busqueda automatica del IWAD

Sin launcher, el arranque debe buscar exactamente en este orden:

~~~text
ux0:/data/DSDA-Doom/IWADs
uma0:/data/DSDA-Doom/IWADs
ur0:/data/DSDA-Doom/IWADs
~~~

Dentro de cada directorio se buscaran archivos *.wad sin distinguir mayusculas/minusculas.

La seleccion debe ser determinista: GLOB_FLAG_SORTED y primer resultado por nombre.

Orden global:

1. primer WAD alfabetico de ux0;
2. si no existe ninguno, primer WAD alfabetico de uma0;
3. si no existe ninguno, primer WAD alfabetico de ur0;
4. si no existe ninguno, mostrar error y salir.

No se debe usar para esta seleccion inicial la lista standard_iwads de DSDA, porque el requisito del port es aceptar el primer WAD que haya en la carpeta IWADs, incluyendo nombres personalizados.

La funcion Vita debe ejecutarse antes del fallback tradicional de FindIWADFile, salvo que se haya proporcionado explicitamente -iwad. Mantener -iwad operativo sera util para pruebas y para el futuro launcher.

### Raiz de datos escribibles

Propuesta:

- la particion donde se encuentra el IWAD es la raiz preferida;
- si se puede escribir, usar <particion>:/data/DSDA-Doom para config, saves, demos, screenshots, logs y temp;
- si no se puede escribir, usar ux0:/data/DSDA-Doom como fallback;
- ur0 debe considerarse en primer lugar como fuente de lectura, no asumir que sea el destino correcto para datos modificables.

Estructura objetivo:

~~~text
<data-root>:/data/DSDA-Doom/
    IWADs/
    PWADs/
    Saves/
    Demos/
    Screenshots/
    Temp/
    Logs/
    dsda-doom.cfg
~~~

DSDA internamente crea dsda_doom_data para datos organizados. Se puede mantener esa logica debajo de la raiz Vita o redirigirla de forma limpia; no hace falta eliminar el organizador de datos.

### Problema con PATH_SEPARATOR

En Unix DSDA usa ":" como separador de listas de rutas.

Eso entra en conflicto con rutas Vita:

~~~text
ux0:/data/DSDA-Doom
~~~

Por tanto el codigo Vita NO debe parsear DOOMWADPATH/XDG_DATA_DIRS usando ":".

La rama __vita__ debe evitar la logica XDG/POSIX de escritorio y usar una lista de rutas Vita explicita.

---

## 7. dsda-doom.wad interno

dsda-doom.wad NO es el IWAD del usuario. Es el WAD interno de recursos de DSDA y debe estar siempre disponible.

Upstream ya contempla cross-compiling su WAD mediante un ExternalProject de host. Esto debe conservarse.

En Vita:

- construir dsda-doom.wad con una herramienta host, no ARM;
- empaquetarlo dentro del VPK;
- ruta recomendada: app0:/dsda-doom.wad;
- agregar una ruta Vita explicita en la busqueda del port WAD si SDL_GetBasePath no resulta suficiente.

No copiar dsda-doom.wad dentro de IWADs.

---

## 8. Presentacion SOFTWARE mediante VitaGL

### Resolucion inicial

Renderer software interno:

~~~text
960x544
~~~

Salida fisica VitaGL:

~~~text
960x544
~~~

No hay escalado en la primera version.

### Superficies

DSDA trabaja con una pantalla indexada de 8 bits.

La arquitectura recomendada:

1. screen: SDL_Surface de 8 bits a SCREENWIDTH x SCREENHEIGHT;
2. textura VitaGL RGBA de SCREENWIDTH x SCREENHEIGHT;
3. buffer: SDL_Surface de 32 bits cuyo pixels apunta a la memoria de la textura VitaGL cuando sea seguro;
4. SDL_LowerBlit convierte la pantalla paletizada directamente a la memoria RGBA de la textura;
5. VitaGL dibuja la textura a la pantalla;
6. vglSwapBuffers presenta.

Esto evita una copia extra tipo:

~~~text
8-bit -> 32-bit RAM -> glTexSubImage -> VRAM
~~~

y busca el camino:

~~~text
8-bit -> SDL_LowerBlit -> memoria de textura VitaGL -> pantalla
~~~

VitaGL expone vglGetTexDataPointer(GL_TEXTURE_2D), que permite obtener el puntero a los datos internos de la textura.

La paleta sigue perteneciendo a la ruta software de DSDA. I_UploadNewPalette debe permanecer activa para VID_MODESW.

### Inicializacion grafica

El contexto VitaGL debe inicializarse siempre a 960x544 porque esa es la pantalla fisica.

Para el modo software se recomienda sin MSAA: no aporta calidad al quad final y consume memoria.

Conceptualmente:

~~~text
vglInitExtended(..., 960, 544, ..., SCE_GXM_MULTISAMPLE_NONE)
~~~

El texture filter inicial debe ser GL_NEAREST.

---

## 9. Resolucion reducida para un launcher futuro

Desde el primer port se deben separar:

~~~text
internal_width / internal_height
display_width  = 960
display_height = 544
~~~

SCREENWIDTH y SCREENHEIGHT representan la resolucion interna software.

VitaGL siempre representa en 960x544.

Perfiles recomendados que conservan exactamente la relacion de aspecto fisica 30:17:

| Perfil | Interna | Escala aproximada |
|---|---:|---:|
| Native | 960x544 | 1.00 |
| Balanced | 720x408 | 0.75 |
| Performance | 480x272 | 0.50 |
| Low | 360x204 | 0.375 |

480x272 es especialmente util porque escala exactamente 2x a 960x544 con nearest.

720x408 conserva exactamente el aspect ratio, aunque su escalado a 960x544 es 4/3 y nearest no tendra pixels uniformes. Para ese perfil puede ofrecerse filtrado lineal en el futuro.

Resoluciones 16:9 como 640x360 no coinciden exactamente con la pantalla 960x544. Si se ofrecen, el presentador debe mantener aspect ratio y dejar barras de pocos pixels en lugar de deformar la imagen.

El launcher futuro no debe cambiar la resolucion fisica del contexto VitaGL. Solo debe seleccionar:

- renderer = software / opengl;
- software_width;
- software_height;
- filtro nearest / linear;
- IWAD.

Para una primera version del launcher es preferible seleccionar renderer al arrancar, no hacer hot-switch dentro del proceso.

---

## 10. SDL2 GameController en Vita

DSDA actual ya usa SDL_GameController.

SDL2 2.32.x tiene backend Vita y una entrada en su base de mappings para "PSVita Controller".

El mapping observado incluye:

- Cross / Circle / Square / Triangle;
- D-pad;
- Select / Start;
- L1 / R1;
- sticks izquierdo y derecho;
- L2 / R2 como ejes;
- L3 / R3 donde esten disponibles.

Por tanto no hace falta reemplazar el input por sceCtrl en la primera etapa.

De todos modos, el log inicial debe registrar:

~~~text
SDL_NumJoysticks()
SDL_JoystickNameForIndex(0)
SDL_IsGameController(0)
SDL_GameControllerName(...)
~~~

Si SDL_IsGameController(0) es falso en una build concreta, se puede inyectar el mapping de Vita antes de abrirlo.

---

## 11. Audio

### Primer bring-up

Arrancar primero con:

~~~text
-nosound -nomusic
~~~

Esto reduce variables mientras se valida filesystem, IWAD, render y controles.

### Dependencias base

SDL2_mixer y libsndfile existen para Vita.

SFX de DSDA pasan por SDL audio / SDL_mixer y libsndfile, por lo que no se preve un port completo del mezclador.

### Opcionales

Recomendacion inicial de CMake:

~~~text
WITH_IMAGE=OFF
WITH_MAD=OFF
WITH_FLUIDSYNTH=OFF
WITH_PORTMIDI=OFF
WITH_VORBISFILE=OFF
WITH_XMP=OFF
~~~

Despues habilitar uno por uno.

libxmp ya contiene adaptaciones de rutas temporales para VitaSDK.

Vorbis tambien esta disponible.

libmad existe, pero debe vigilarse ABI de enums y alineacion.

### FluidSynth

VitaSDK ofrece fluidsynth-lite basado en una API antigua. Tiene el sintetizador y fluid_synth_write_s16, pero DSDA moderno usa fluid_sfloader_set_callbacks para cargar SoundFonts desde memoria/lumps. Esa API no esta expuesta de la misma forma en fluidsynth-lite.

Opciones futuras:

1. mantener FluidSynth desactivado;
2. extraer temporalmente el lump SNDFONT a Temp y llamar fluid_synth_sfload sobre un archivo;
3. portar/adaptar el loader de callbacks.

No bloquea el primer port.

### PortMidi

No es necesario para reproducir Doom en Vita. Mantener WITH_PORTMIDI=OFF.

---

## 12. Funciones de escritorio que deben desactivarse/adaptarse

### Captura de video

i_capture.c usa en POSIX:

- pipe;
- fork;
- dup2;
- execl /bin/sh;
- waitpid.

Eso no debe compilarse como ruta POSIX normal en Vita.

En __vita__, -viddump debe devolver un mensaje de "no soportado" de forma limpia.

La captura mediante encoder embebido seria un proyecto separado.

### Senales

SDL/i_main.c registra SIGSEGV, SIGFPE, SIGILL, SIGABRT, SIGTERM y SIGINT.

Para el primer port Vita conviene proteger esta instalacion con !defined(__vita__) y priorizar logging/checkpoints de arranque.

### mmap

No es bloqueo.

DSDA ya selecciona w_memcache.c cuando HAVE_MMAP es falso.

Eso aumenta potencialmente uso de RAM con WAD grandes, por lo que se debe medir memoria en mapas pesados, pero permite arrancar sin implementar mmap.

---

## 13. Memoria

Z_Malloc actual termina usando malloc del runtime.

Para Vita hay que definir explicitamente tamanos razonables de stack/heap en el ejecutable.

Referencia inicial conservadora proveniente de ports PrBoom Vita:

~~~text
sceUserMainThreadStackSize = 1 MiB
_newlib_heap_size_user     = 128 MiB
~~~

No incrementar por intuicion. Primero registrar uso real y subir solo si DSDA/WADs grandes lo requieren.

A 960x544:

- framebuffer indexado: aproximadamente 0.5 MiB;
- textura RGBA: aproximadamente 2 MiB;
- DSDA mantiene varias screens software, por lo que el total de buffers es mayor;
- sigue siendo razonable frente al presupuesto Vita, pero el cuello de botella esperado es CPU antes que el framebuffer.

---

## 14. Rendimiento

960x544 tiene 522240 pixels.

320x200 tiene 64000 pixels.

El renderer software a nativo procesa mas de ocho veces el numero de pixels de la resolucion clasica.

Por requisito, el primer port arrancara en 960x544, pero hay que instrumentar:

- tiempo de R_RenderPlayerView;
- tiempo de I_FinishUpdate;
- conversion SDL_LowerBlit;
- FPS;
- memoria libre/heap;
- pitch elegido;
- numero de segs/planes/sprites si el HUD de estadisticas de DSDA lo permite.

No introducir NEON antes de medir.

Posibles optimizaciones futuras, solo si el profiler las justifica:

- conversion de paleta;
- R_DrawColumn;
- R_DrawSpan;
- flush de columnas;
- clears/memcpy;
- resolucion interna inferior.

---

## 15. Resolucion y SCREENPITCH

DSDA intenta medir dos pitches y escoger el mas rapido mediante I_TestCPUCacheMisses.

En Vita puede mantenerse inicialmente, pero la prueba introduce trabajo extra durante startup.

Para estabilizar el primer VPK se puede usar un pitch alineado fijo:

~~~text
SCREENPITCH = ALIGN_UP(SCREENWIDTH, 16)
~~~

y perfilar despues si +32 mejora el Cortex-A9.

No asumir que la heuristica creada para Pentium 4 sea optima en Vita.

---

## 16. VPK sin launcher

El VPK inicial debe arrancar directamente DSDA.

Contenido minimo:

~~~text
EBOOT.BIN
dsda-doom.wad
sce_sys/...
~~~

Los IWAD comerciales NO se empaquetan.

El usuario coloca el WAD en una de:

~~~text
ux0:/data/DSDA-Doom/IWADs/
uma0:/data/DSDA-Doom/IWADs/
ur0:/data/DSDA-Doom/IWADs/
~~~

No se debe incluir vlauncher.self ni un binario intermedio.

El TITLEID todavia debe fijarse para el proyecto; debe ser una variable CMake clara y no quedar disperso por varios archivos.

---

## 17. CI / GitHub Actions

Usar VitaSDK actual y su frontend moderno:

~~~text
vdpm install <paquetes>
~~~

No depender de paquetes del sistema host para ARM.

El WAD interno dsda-doom.wad debe compilarse como herramienta host durante cross-compile, usando el mecanismo ExternalProject que ya trae upstream.

La CI debe conservar artefactos separados:

- ELF;
- SELF;
- VPK;
- dsda-doom.wad;
- log de CMake;
- mapa de enlace opcional para diagnostico.

---

## 18. Estrategia para el futuro renderer OpenGL

No eliminar el codigo OpenGL de DSDA.

La separacion deseada a futuro:

~~~text
                  Vita launcher
                       |
             +---------+---------+
             |                   |
        SOFTWARE               OPENGL
             |                   |
      renderer CPU        renderer gld_* portado
             |                   |
   textura de presentacion       |
             |                   |
             +---------+---------+
                       |
                     VitaGL
                       |
                    960x544
~~~

El launcher futuro seleccionara modo antes de ejecutar.

Para SOFTWARE:
- V_InitMode(VID_MODESW);
- framebuffer 8-bit;
- presentador VitaGL.

Para OPENGL:
- V_InitMode(VID_MODEGL);
- gld_* portado sobre VitaGL;
- no usar el framebuffer software como escena principal.

De esta manera ambos modos pueden coexistir sin duplicar filesystem, input, audio, WAD loading ni UI de configuracion.

---

## 19. Riesgos principales ordenados

1. Separar correctamente dependencias GL del build software.
2. Referencias gld_* en codigo comun que producen errores de linker.
3. Rutas Vita y conflicto del caracter ":".
4. Accesos desalineados en ARM.
5. Coste de 960x544 en renderer software.
6. Integracion VitaGL sin copia extra del framebuffer.
7. SDL2_mixer/runtime de musica en Vita.
8. Uso de RAM con w_memcache y WADs grandes.
9. Funciones POSIX de escritorio que se cuelen en el build.
10. Port futuro del renderer OpenGL.

---

## 20. Criterio de exito de la primera etapa

La primera etapa del port se considera funcional cuando:

- el VPK arranca directamente;
- VitaGL inicializa a 960x544;
- DSDA permanece en VID_MODESW;
- encuentra automaticamente el primer WAD en la lista de particiones;
- encuentra app0:/dsda-doom.wad;
- entra al menu;
- inicia un mapa;
- paredes, pisos, techos, sprites, HUD, automap y menus se ven correctamente;
- el mando Vita controla el juego;
- no hay dependencia de desktop OpenGL/GLU;
- no hay launcher;
- no hay crash al cambiar paleta, wipe o mapa;
- se genera un log util de startup;
- el codigo deja lista la separacion internal resolution / physical resolution.

---

## Referencias de codigo revisadas

DSDA-Doom:

- prboom2/cmake/DsdaDependencies.cmake
- prboom2/cmake/DsdaDepsSetup.cmake
- prboom2/cmake/DsdaConfigHeader.cmake
- prboom2/cmake/DsdaTargetFeatures.cmake
- prboom2/src/CMakeLists.txt
- prboom2/src/d_main.c
- prboom2/src/v_video.c
- prboom2/src/SDL/i_video.c
- prboom2/src/SDL/i_system.c
- prboom2/src/SDL/i_main.c
- prboom2/src/SDL/i_sound.c
- prboom2/src/i_capture.c
- prboom2/src/i_glob.c
- prboom2/src/dsda/data_organizer.c
- prboom2/src/dsda/save.c
- prboom2/src/dsda/zipfile.c
- prboom2/src/dsda/game_controller.c
- prboom2/src/dsda/cr_table.c
- prboom2/src/r_main.c
- prboom2/src/r_plane.c
- prboom2/src/r_drawflush.inl
- prboom2/src/z_zone.c
- prboom2/src/w_memcache.c

VitaSDK / SDL / VitaGL:

- vitasdk/vita-toolchain - cmake_toolchain/vita.toolchain.cmake
- vitasdk/packages - sdl2/VITABUILD
- vitasdk/packages - sdl2_mixer/VITABUILD
- vitasdk/packages - libsndfile/VITABUILD
- vitasdk/packages - libzip/VITABUILD
- vitasdk/packages - vitaGL/VITABUILD
- libsdl-org/SDL release-2.32.x - src/joystick/vita/SDL_sysjoystick.c
- libsdl-org/SDL release-2.32.x - src/joystick/SDL_gamecontrollerdb.h
- Rinnegatamante/vitaGL - source/vitaGL.h
- Rinnegatamante/vitaGL - Makefile
