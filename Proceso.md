# Vita-DSDA-Doom - Proceso completo de port a PS Vita

Este documento describe el proceso recomendado, de principio a fin, para portar el DSDA-Doom actual a PS Vita usando exclusivamente el renderer SOFTWARE durante la primera etapa y VitaGL como backend de presentacion.

La arquitectura se diseña desde el principio para permitir en el futuro:

- renderer SOFTWARE de DSDA;
- renderer OpenGL de DSDA portado a VitaGL;
- seleccion de renderer desde un launcher;
- seleccion de IWAD desde launcher;
- seleccion de resolucion interna software desde launcher.

Durante esta primera etapa NO se desarrolla launcher y NO se desarrolla el renderer OpenGL de DSDA.

---

# 0. Resultado objetivo de esta primera fase

Al instalar y ejecutar el VPK:

1. arranca directamente DSDA-Doom;
2. inicializa SDL;
3. inicializa VitaGL a 960x544;
4. fuerza DSDA a VID_MODESW;
5. busca el IWAD en:
   - ux0:/data/DSDA-Doom/IWADs
   - uma0:/data/DSDA-Doom/IWADs
   - ur0:/data/DSDA-Doom/IWADs
6. usa el primer *.wad encontrado, con seleccion determinista;
7. carga app0:/dsda-doom.wad como WAD interno del port;
8. renderiza el juego por CPU a 960x544;
9. convierte el framebuffer indexado a RGBA;
10. presenta el resultado con VitaGL a 960x544;
11. usa SDL2 para input;
12. genera logs suficientes para diagnosticar startup, IWAD, video y controller;
13. no necesita OpenGL/GLU de escritorio;
14. no usa launcher.

---

# 1. Crear las opciones de build para separar renderer y presentador

## 1.1 Problema actual

El CMake upstream mezcla:

- renderer software;
- renderer OpenGL;
- dependencia de OpenGL/GLU;
- codigo comun con llamadas gld_*.

Eso impide compilar un binario puramente software para Vita aunque runtime use VID_MODESW.

## 1.2 Opciones nuevas

Agregar opciones conceptualmente equivalentes a:

~~~cmake
option(DSDA_ENABLE_OPENGL_RENDERER "Build DSDA OpenGL renderer" ON)
option(DSDA_VITA "Build for PlayStation Vita" OFF)
option(DSDA_VITA_PRESENT_VITAGL "Use VitaGL to present the software framebuffer" OFF)
~~~

En un toolchain Vita, DSDA_VITA puede activarse automaticamente mediante VITA / BUILD_VITA.

Configuracion inicial:

~~~text
DSDA_ENABLE_OPENGL_RENDERER=OFF
DSDA_VITA=ON
DSDA_VITA_PRESENT_VITAGL=ON
~~~

En desktop, DSDA_ENABLE_OPENGL_RENDERER debe seguir ON por defecto.

---

# 2. Adaptar DsdaDependencies.cmake

## 2.1 Mantener obligatorias

Para el primer port:

- SDL2;
- SDL2_mixer;
- libsndfile;
- ZLIB;
- libzip.

## 2.2 OpenGL

Mover:

~~~cmake
find_package(OpenGL 2.0 REQUIRED)
~~~

y el link de:

~~~text
OpenGL::GL
OpenGL::GLU
~~~

dentro de:

~~~cmake
if(DSDA_ENABLE_OPENGL_RENDERER)
    ...
endif()
~~~

## 2.3 VitaGL

No modelar VitaGL como OpenGL::GL.

Crear un target separado, por ejemplo:

~~~cmake
vitaGL::vitaGL
~~~

o un target imported local.

El presentador software depende de VitaGL, pero el renderer software sigue siendo VID_MODESW.

## 2.4 Dependencias opcionales del primer bring-up

Desactivar:

~~~text
WITH_IMAGE=OFF
WITH_MAD=OFF
WITH_FLUIDSYNTH=OFF
WITH_PORTMIDI=OFF
WITH_VORBISFILE=OFF
WITH_XMP=OFF
~~~

Despues se reactivan individualmente.

---

# 3. Integrar VitaSDK correctamente

## 3.1 Toolchain

Invocar CMake con:

~~~text
-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
~~~

VitaSDK ya fija:

~~~text
arm-vita-eabi-gcc
arm-vita-eabi-g++
armv7-a
~~~

## 3.2 Flags

Primera configuracion:

~~~text
-O3
-mtune=cortex-a9
-mfpu=neon
-fsigned-char
~~~

Evitar inicialmente:

~~~text
-fno-short-enums
~~~

salvo que aparezca un problema ABI demostrado.

## 3.3 vita.cmake

Incluir:

~~~cmake
include("$ENV{VITASDK}/share/vita.cmake")
~~~

para disponer de:

- vita_create_self;
- vita_create_vpk.

---

# 4. Integrar VitaGL de forma reproducible

## 4.1 Por que conviene una copia local/build propio

El paquete binario estandar de VitaGL no activa ENABLE_LEGACY_PIPELINE.

Para evitar una diferencia entre el presentador actual y el futuro renderer GL de DSDA:

- usar una revision fijada de VitaGL;
- compilarla dentro de CI;
- compilar vitaShaRK correspondiente;
- enlazar SceShaccCgExt, taihen y libmathneon.

## 4.2 Flags VitaGL recomendados

Durante desarrollo:

~~~text
ENABLE_LEGACY_PIPELINE=1
NO_SPLASHSCREEN=1
LOG_ERRORS=1
HAVE_PROFILING=1
~~~

Release futuro:

~~~text
ENABLE_LEGACY_PIPELINE=1
NO_SPLASHSCREEN=1
NO_DEBUG=1
~~~

## 4.3 Inicializacion

Inicializar una sola vez al iniciar el ejecutable:

~~~text
Vita_VideoInit()
    -> vglInitExtended(..., 960, 544, ..., SCE_GXM_MULTISAMPLE_NONE)
~~~

No recrear VitaGL cuando cambie la resolucion interna software.

Cerrar solamente al salir:

~~~text
Vita_VideoShutdown()
~~~

---

# 5. Crear una capa Vita separada

Crear:

~~~text
prboom2/src/vita/
    vita_system.c
    vita_system.h
    vita_video.c
    vita_video.h
    vita_iwad.c
    vita_iwad.h
~~~

Opcional:

~~~text
vita_log.c
vita_log.h
~~~

No saturar SDL/i_video.c e i_system.c con cientos de lineas #ifdef.

## 5.1 API de video sugerida

~~~c
void Vita_VideoInit(void);
void Vita_VideoShutdown(void);

void Vita_SWCreate(int width, int height);
void Vita_SWDestroy(void);
void Vita_SWPresent(SDL_Surface *screen);

void Vita_SetInternalResolution(int width, int height);

int Vita_DisplayWidth(void);   // 960
int Vita_DisplayHeight(void);  // 544
~~~

## 5.2 API filesystem sugerida

~~~c
const char *Vita_FindAutoIWAD(void);
const char *Vita_DataRoot(void);
const char *Vita_TempDir(void);
const char *Vita_LogDir(void);
~~~

---

# 6. Separar renderer interno y salida fisica

Definir claramente:

~~~text
software_width
software_height

display_width  = 960
display_height = 544
~~~

Inicialmente:

~~~text
software_width  = 960
software_height = 544
~~~

SCREENWIDTH y SCREENHEIGHT deben continuar significando resolucion interna de DSDA.

Nunca reutilizar SCREENWIDTH como ancho fisico de la pantalla Vita.

---

# 7. Forzar VID_MODESW en el primer port

En Vita:

~~~text
I_DesiredVideoMode() -> VID_MODESW
~~~

aunque el usuario tenga:

~~~text
videomode = gl
~~~

en un cfg copiado de desktop.

Log obligatorio:

~~~text
[VITA] DSDA renderer: SOFTWARE
[VITA] presentation backend: VitaGL
~~~

No devolver VID_MODEGL solo porque se usa VitaGL.

---

# 8. Hacer condicional el renderer OpenGL de DSDA

## 8.1 Fuentes gl_*

Mover todas bajo:

~~~cmake
if(DSDA_ENABLE_OPENGL_RENDERER)
    target_sources(...)
endif()
~~~

## 8.2 Cabeceras comunes

Revisar y proteger:

- SDL_opengl.h;
- gl_struct.h;
- gl_intern.h;
- gl_opengl.h.

## 8.3 Referencias gld_* fuera de gl_*

Cada llamada debe quedar:

~~~c
#if DSDA_ENABLE_OPENGL_RENDERER
    if (V_IsOpenGLMode()) {
        ...
    }
#endif
~~~

o usar wrappers no-op bien definidos.

Preferencia: compile guards reales.

Evitar un gran gl_stub.c si puede eliminarse la dependencia limpiamente.

## 8.4 v_video.c

Mantener:

~~~text
VID_MODESW
VID_MODEGL
~~~

en el enum para no destruir arquitectura futura.

Pero si DSDA_ENABLE_OPENGL_RENDERER=OFF:

- compilar solamente implementaciones software;
- no referenciar WRAP_gld_*;
- si se intenta V_InitMode(VID_MODEGL), producir error claro.

---

# 9. Implementar seleccion automatica de IWAD

## 9.1 Orden de particiones

Exactamente:

~~~text
1. ux0
2. uma0
3. ur0
~~~

## 9.2 Directorio

~~~text
/data/DSDA-Doom/IWADs
~~~

## 9.3 Algoritmo

Pseudocodigo:

~~~c
static const char *roots[] = {
    "ux0:/data/DSDA-Doom/IWADs",
    "uma0:/data/DSDA-Doom/IWADs",
    "ur0:/data/DSDA-Doom/IWADs",
};

for each root:
    glob = I_StartGlob(root, "*.wad", GLOB_FLAG_NOCASE | GLOB_FLAG_SORTED)

    first = I_NextGlob(glob)

    if first:
        return copy(first)
~~~

Usar el globber existente de DSDA si dirent funciona correctamente con VitaSDK.

## 9.4 Integracion en FindIWADFile

Orden:

~~~text
si existe -iwad:
    respetar -iwad

si __vita__:
    Vita_FindAutoIWAD()

si no encontro:
    fallback tradicional de DSDA
~~~

Esto permite que un launcher futuro pase:

~~~text
-iwad <ruta>
~~~

sin cambiar nuevamente IdentifyVersion.

## 9.5 Validacion

No exigir nombres clasicos.

CheckIWAD ya inspecciona el contenido y determina gamemode.

Esto permite:

~~~text
mydoom.wad
customiwad.wad
freedoom2.wad
doom2.wad
~~~

si son IWAD validos.

---

# 10. Resolver la raiz de datos Vita

## 10.1 Detectar particion del IWAD

Si IWAD viene de:

~~~text
uma0:/...
~~~

intentar usar:

~~~text
uma0:/data/DSDA-Doom
~~~

como raiz de datos.

## 10.2 Verificar escritura

Crear o probar:

~~~text
<root>/Logs
<root>/Temp
~~~

Si falla:

~~~text
fallback = ux0:/data/DSDA-Doom
~~~

## 10.3 Directorios

Crear:

~~~text
IWADs
PWADs
Saves
Demos
Screenshots
Temp
Logs
~~~

sin fallar si ya existen.

## 10.4 I_ConfigDir

En Vita:

~~~text
I_ConfigDir() -> <data-root>
~~~

## 10.5 I_GetTempDir

~~~text
I_GetTempDir() -> <data-root>/Temp
~~~

## 10.6 I_ExeDir

No usar como equivalente directo de datos.

app0 es de recursos empaquetados.

---

# 11. Eliminar el conflicto ":" de las rutas Vita

No ejecutar en Vita el parser POSIX de listas:

~~~text
XDG_DATA_DIRS
DOOMWADPATH
~~~

que usa ":" como separador.

Una ruta:

~~~text
ux0:/data/DSDA-Doom
~~~

se romperia.

En __vita__:

- no usar XDG;
- no usar HOME;
- no usar PATH_SEPARATOR=":";
- agregar explicitamente las rutas Vita relevantes.

---

# 12. Integrar dsda-doom.wad

## 12.1 Compilacion

Conservar el generador de WAD host de upstream.

Cross build:

~~~text
host compiler
    -> dsda_rdatawad
    -> dsda-doom.wad

arm-vita-eabi
    -> EBOOT
~~~

## 12.2 VPK

Empaquetar:

~~~text
app0:/dsda-doom.wad
~~~

## 12.3 Busqueda

Agregar ruta Vita explicita o asegurar que I_GetBasePath resuelva app0.

Log:

~~~text
[VITA] port wad: app0:/dsda-doom.wad
~~~

---

# 13. Adaptar SDL/i_system.c

Crear una rama:

~~~c
#elif defined(__vita__)
~~~

antes del POSIX generico.

Implementar:

- I_ConfigDir;
- I_ExeDir;
- I_GetTempDir;
- I_GetHomeDir equivalente, si alguna interfaz lo necesita;
- I_FindFile search table Vita.

Evitar:

- XDG;
- getpwuid;
- /tmp;
- rutas /usr/share.

---

# 14. Adaptar i_capture.c

## 14.1 Vita

No compilar la implementacion POSIX que usa:

- fork;
- pipe;
- dup2;
- execl;
- waitpid.

## 14.2 Comportamiento

En Vita:

~~~text
-viddump
~~~

debe:

- imprimir warning;
- no crash;
- dejar capturing_video = 0.

No bloquear screenshots normales.

---

# 15. Adaptar manejo de signals

En SDL/i_main.c:

~~~c
#if !defined(__vita__)
    signal(...)
#endif
~~~

Mantener un crash log propio donde sea posible.

No depender de un handler POSIX para recuperar informacion util en Vita.

---

# 16. Crear logging Vita desde el primer commit funcional

Archivo sugerido:

~~~text
<data-root>/Logs/dsda-vita.log
~~~

Registrar:

~~~text
build SHA
DSDA version
VitaSDK info si es accesible
IWAD search paths
IWAD seleccionado
data root seleccionado
port WAD path
internal resolution
physical resolution
SCREENPITCH
VitaGL init success/failure
texture format
texture pointer
SDL_NumJoysticks
SDL_IsGameController
SDL controller name
audio enabled/disabled
heap/stack configuration
startup milestones
~~~

Milestones:

~~~text
BOOT
SDL INIT
VITAGL INIT
FILESYSTEM INIT
IWAD FOUND
PORT WAD FOUND
CONFIG LOADED
VIDEO MODE INIT
CONTROLLER INIT
DOOM MAIN
MENU
MAP LOAD
FIRST FRAME
~~~

---

# 17. Implementar framebuffer software a 960x544

## 17.1 Surface indexada

Crear:

~~~c
screen = SDL_CreateRGBSurface(
    0,
    SCREENWIDTH,
    SCREENHEIGHT,
    8,
    0, 0, 0, 0
);
~~~

En primera etapa:

~~~text
SCREENWIDTH  = 960
SCREENHEIGHT = 544
~~~

## 17.2 screens[0]

Idealmente mantener la relacion actual:

- screens[0].data pertenece al renderer;
- screen se usa como superficie SDL para paleta/blit.

Si puede hacerse direct access seguro, reutilizar pixels. Si no, copiar respetando pitch.

Primero priorizar correccion.

---

# 18. Crear textura VitaGL

Crear textura 32-bit:

~~~text
width  = internal_width
height = internal_height
format = RGBA/BGRA compatible con SDL surface elegida
filter = nearest
wrap   = clamp
~~~

Obtener:

~~~c
void *pixels = vglGetTexDataPointer(GL_TEXTURE_2D);
~~~

Crear SDL_Surface de 32 bits que use ese puntero si el layout/pitch de VitaGL es lineal y compatible.

Verificar siempre:

- pointer no NULL;
- pitch esperado;
- alignment;
- channel order.

Si no puede garantizarse layout directo en una revision concreta de VitaGL, fallback:

~~~text
SDL_LowerBlit -> buffer RGBA CPU
glTexSubImage2D -> textura
~~~

El fallback debe existir durante bring-up para distinguir bugs de renderer de bugs de zero-copy.

---

# 19. I_FinishUpdate en Vita

Ruta objetivo:

~~~c
if (V_IsSoftwareMode()) {
    actualizar paleta si hace falta;

    SDL_LowerBlit(
        screen,
        &src_rect,
        buffer,
        &src_rect
    );

    Vita_SWPresent(buffer);

    I_HandleCapture();

    return;
}
~~~

Vita_SWPresent:

1. bind textura;
2. limpiar framebuffer;
3. fijar viewport 960x544;
4. dibujar fullscreen quad;
5. vglSwapBuffers(GL_FALSE).

No usar:

- SDL_Renderer;
- SDL_Texture;
- SDL_RenderCopy;
- SDL_RenderPresent.

---

# 20. Quad de presentacion

Con salida 960x544 e interna 960x544:

~~~text
dst = 0,0 - 960,544
uv  = 0,0 - 1,1
~~~

Estado GL minimo:

~~~text
depth off
blend off
texture 2D on
viewport 960x544
orthographic projection
~~~

Filtrado inicial:

~~~text
GL_NEAREST
~~~

Cuando aparezcan resoluciones inferiores el mismo quad se escala.

---

# 21. Preparar resolucion dinamica futura

Crear una estructura:

~~~c
typedef struct {
    int internal_width;
    int internal_height;
    int display_width;
    int display_height;
    int filter_mode;
} vita_video_config_t;
~~~

Primera configuracion:

~~~text
960
544
960
544
NEAREST
~~~

El launcher futuro solo tiene que producir una configuracion distinta.

No codificar 960/544 dispersos en varias funciones.

---

# 22. Perfiles de resolucion futuros

Agregar tabla desde el principio aunque solo se active Native:

~~~text
native      960x544
balanced    720x408
performance 480x272
low         360x204
~~~

Para la fase actual:

~~~text
selected = native
~~~

No exponer menu todavia.

## 22.1 Cambio de resolucion

Al cambiar interna:

1. Vita_SWDestroy;
2. V_FreeScreens;
3. actualizar SCREENWIDTH/SCREENHEIGHT;
4. recalcular SCREENPITCH;
5. V_AllocScreens;
6. recrear surface 8-bit;
7. recrear textura RGBA;
8. recrear surface wrapper;
9. recalcular aspect/stretches;
10. continuar render.

No destruir VitaGL.

---

# 23. Aspect ratio

Pantalla Vita:

~~~text
960 / 544 = 30 / 17
~~~

Usar preferentemente resoluciones con esa relacion.

Si futura resolucion no coincide:

~~~text
scale = min(
    960 / internal_width,
    544 / internal_height
)
~~~

calcular viewport centrado:

~~~text
dst_w = internal_width  * scale
dst_h = internal_height * scale
dst_x = (960 - dst_w) / 2
dst_y = (544 - dst_h) / 2
~~~

Limpiar negro alrededor.

No deformar.

---

# 24. Revisar I_CalculateRes y SCREENPITCH

Upstream ejecuta un microbenchmark de pitch durante startup.

Para la primera Vita build considerar:

~~~text
SCREENPITCH = (SCREENWIDTH + 15) & ~15
~~~

A 960 ya es multiplo de 16.

A 720 tambien.

A 480 tambien.

A 360 se alinearia a 368.

Despues comparar contra:

~~~text
aligned + 32
~~~

con profiler.

---

# 25. Corregir accesos desalineados ARM conocidos

Antes del primer test serio corregir:

- dsda/cr_table.c;
- v_video.c;
- g_overflow.c donde corresponda.

Helpers sugeridos:

~~~c
static uint16_t ReadU16Unaligned(const void *ptr)
{
    uint16_t v;
    memcpy(&v, ptr, sizeof(v));
    return v;
}

static uint32_t ReadU32Unaligned(const void *ptr)
{
    uint32_t v;
    memcpy(&v, ptr, sizeof(v));
    return v;
}
~~~

Aplicar LittleShort/LittleLong despues.

Crear una build CI temporal con:

~~~text
-Wcast-align
~~~

sin necesariamente convertirlo en error.

---

# 26. Input

## 26.1 Usar SDL GameController

No crear un backend sceCtrl propio inicialmente.

DSDA ya tiene:

~~~text
dsda/game_controller.c
~~~

SDL2 Vita ya expone:

~~~text
PSVita Controller
~~~

## 26.2 Diagnostico

Al iniciar:

~~~text
joysticks = SDL_NumJoysticks()
name      = SDL_JoystickNameForIndex(0)
mapped    = SDL_IsGameController(0)
~~~

Si mapped es false:

- inyectar mapping con SDL_GameControllerAddMapping;
- reintentar.

## 26.3 Mapping inicial recomendado

Definir un preset Vita en cfg o defaults:

~~~text
left stick  -> movimiento
right stick -> giro/look
Cross       -> use
R           -> fire
L           -> run/secondary configurable
Triangle    -> weapon/menu action
Circle      -> back
Square      -> configurable
D-pad       -> weapon/menu
Start       -> menu/pause
Select      -> automap
~~~

No fijar demasiado pronto acciones si DSDA ya tiene defaults controller razonables.

---

# 27. Touch

No es requisito para el primer port.

SDL Vita tiene backend touch.

Dejarlo fuera inicialmente.

Posibles usos futuros:

- menu;
- automap;
- launcher;
- teclado virtual.

---

# 28. Audio por etapas

## Etapa A

Ejecutar:

~~~text
-nosound -nomusic
~~~

Validar primero:

- startup;
- video;
- input;
- mapa.

## Etapa B

Activar SFX.

Comprobar:

- SDL audio init;
- libsndfile;
- mezcla;
- latencia;
- cambios de mapa.

## Etapa C

Activar musica OPL primero si es estable.

## Etapa D

Activar:

- Vorbis;
- XMP;
- MP3 si hace falta.

## Etapa E

Adaptar FluidSynth.

PortMidi queda fuera.

---

# 29. Memoria

Definir en una unidad Vita:

~~~c
SceUInt32 sceUserMainThreadStackSize = 1 * 1024 * 1024;
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;
~~~

Usar inicialmente esos valores como referencia, no como conclusion final.

Registrar fallos de alloc.

Medir:

- menu;
- Doom II MAP01;
- mapas grandes;
- PWAD grande;
- cambio repetido de mapas.

---

# 30. WAD cache

Si HAVE_MMAP es false, DSDA usa w_memcache.c.

Primera version:

~~~text
HAVE_MMAP=OFF
~~~

No implementar mmap Vita.

Si memoria se convierte en problema real, evaluar despues:

- custom mmap-like IO;
- cache LRU;
- descarga de lumps;
- port de mmap si VitaSDK ofrece primitives adecuadas.

---

# 31. Screenshot

Mantener screenshots funcionales.

I_GrabScreen / I_ScreenShot debe usar la imagen software, no leer el display VitaGL si el renderer es software.

Esto tiene dos ventajas:

- captura exacta de pixels internos;
- funciona tambien a resoluciones menores.

Para futuro renderer OpenGL se usara readback GL.

---

# 32. Wipes

Probar especialmente:

- melt;
- cambios de nivel;
- finale;
- intermission.

La ruta software ya tiene implementacion propia.

No debe entrar gl_wipe.c con DSDA_ENABLE_OPENGL_RENDERER=OFF.

---

# 33. Textscreen / ENDOOM

txt_sdl.c incluye SDL_opengl.h actualmente.

Para Vita software:

- evitar dependencia GL desktop;
- compilar textscreen sin la rama OpenGL;
- si ENDOOM no es util en Vita, permitir desactivarlo limpiamente.

No romper shutdown por intentar abrir una ventana desktop.

---

# 34. CMake del ejecutable

Target Vita:

~~~text
dsda-doom.elf
~~~

Enlazar:

~~~text
SDL2
SDL2_mixer
sndfile
zip
z
vitaGL
vitaShaRK
SceShaccCgExt
taihen
mathneon
pthread
m
c
stdc++
~~~

mas stubs Vita necesarios:

~~~text
SceLibKernel_stub
SceAppMgr_stub
SceSysmodule_stub
SceCtrl_stub
SceTouch_stub
SceAudio_stub
SceGxm_stub
SceDisplay_stub
ScePower_stub
SceShaccCg_stub
SceKernelDmacMgr_stub
SceKernelModulemgr_stub
~~~

Ajustar segun errores reales de linker; no enlazar stubs no usados solo por copiar otra lista.

---

# 35. Crear SELF

~~~cmake
vita_create_self(
    dsda-doom.self
    dsda-doom.elf
)
~~~

Para app normal:

~~~text
UNSAFE opcional solamente si una API realmente lo exige
~~~

No pedir privilegios adicionales sin necesidad.

---

# 36. Crear VPK

Estructura inicial:

~~~text
EBOOT.BIN
dsda-doom.wad
sce_sys/icon0.png
sce_sys/livearea/contents/template.xml
sce_sys/livearea/contents/bg.png
sce_sys/livearea/contents/startup.png
~~~

No incluir IWADs comerciales.

No incluir launcher.

---

# 37. Elegir TITLEID y metadata

Crear variables centrales:

~~~cmake
set(VITA_TITLEID "...")
set(VITA_APP_NAME "Vita-DSDA-Doom")
set(VITA_VERSION "01.00")
~~~

No repetir valores en distintos archivos.

Antes del primer release fijar TITLEID definitivo.

---

# 38. GitHub Actions

## 38.1 Job

Pasos:

1. checkout;
2. instalar/bootstrap VitaSDK;
3. actualizar vdpm;
4. instalar dependencias;
5. build vitaShaRK/VitaGL local;
6. configurar CMake;
7. construir dsda-doom.wad host;
8. compilar ARM;
9. crear SELF;
10. crear VPK;
11. subir artifacts.

## 38.2 Artefactos

Guardar:

~~~text
vita-dsda-doom.vpk
dsda-doom.elf
dsda-doom.self
dsda-doom.wad
CMakeCache.txt
link.map
~~~

## 38.3 Compilacion paralela

Usar Ninja si esta disponible:

~~~text
cmake -G Ninja
ninja
~~~

DSDA no tiene una razon tecnica para depender de Make en el target principal.

El build local de VitaGL/vitaShaRK puede seguir usando make si sus propios proyectos lo requieren.

---

# 39. Primer test: bootstrap minimo

Compilar inicialmente con:

~~~text
software 960x544
VitaGL
-nosound
-nomusic
no OpenGL renderer
no optional music decoders
~~~

Test 1:

- VPK abre;
- log se crea;
- VitaGL inicializa;
- si no hay IWAD, aparece error legible y no crash.

---

# 40. Test de auto-IWAD

Casos:

## Caso A

~~~text
ux0:/data/DSDA-Doom/IWADs/doom2.wad
~~~

Debe elegir ux0.

## Caso B

ux0 vacio y:

~~~text
uma0:/data/DSDA-Doom/IWADs/doom2.wad
~~~

Debe elegir uma0.

## Caso C

solo ur0:

Debe elegir ur0.

## Caso D

dos WAD en una misma carpeta:

~~~text
a.wad
b.wad
~~~

Debe elegir a.wad.

## Caso E

usar -iwad explicitamente:

Debe prevalecer sobre auto seleccion.

---

# 41. Test del port WAD

Eliminar temporalmente dsda-doom.wad del VPK.

Esperado:

- error claro;
- log identifica la ruta buscada.

Volver a incluirlo.

---

# 42. Test del renderer software

Usar Doom II MAP01.

Validar:

- paredes;
- floors;
- ceilings;
- sprites;
- weapon sprites;
- colormaps;
- palette flashes;
- transparency;
- fuzz;
- HUD;
- status bar;
- automap;
- menu;
- wipes.

Comparar screenshots contra desktop software si aparece una diferencia visual.

---

# 43. Test a 960x544

Medir:

~~~text
frame total
R_RenderPlayerView
SDL_LowerBlit
Vita_SWPresent
swap
~~~

Escenarios:

- MAP01;
- escena con muchos sprites;
- vista grande;
- automap;
- menu;
- PWAD pesado.

Registrar FPS minimo/promedio.

---

# 44. Introducir temporalmente 480x272 para validar arquitectura

Aunque el producto inicial use 960x544, hacer una build de diagnostico a:

~~~text
480x272
~~~

Objetivo:

- comprobar que internal resolution y display resolution estan desacopladas;
- comprobar quad 2x;
- comprobar HUD/stretches;
- detectar codigo que asuma 960x544.

No convertirlo todavia en opcion visible.

---

# 45. Validar cambio de resolucion sin reiniciar VitaGL

Test de desarrollo:

~~~text
960x544
-> destroy software buffers
-> 480x272
-> recreate software buffers
-> present
~~~

El contexto VitaGL debe seguir vivo.

Esto prueba la base del futuro launcher/config system.

---

# 46. Audio

Una vez estable video/input:

1. quitar -nosound;
2. probar SFX;
3. cambiar mapas;
4. stress con muchas fuentes;
5. quitar -nomusic;
6. probar OPL;
7. luego activar codecs opcionales uno por uno.

Si SDL2_mixer reproduce un crash, aislar exactamente el decoder antes de reemplazar arquitectura de audio.

---

# 47. Validar controller

Probar:

- sticks;
- D-pad;
- Cross/Circle/Square/Triangle;
- L/R;
- Start;
- Select;
- menu navigation;
- automap;
- gameplay;
- deadzones.

Registrar mapping en log.

---

# 48. Stabilizacion ARM

Despues del primer mapa funcional:

- ejecutar build con -Wcast-align;
- corregir rutas de carga de WAD;
- probar Heretic;
- probar Hexen;
- probar IWAD con nombre personalizado;
- probar PWADs complejos;
- probar demos.

Especial atencion a structs packed y acceso a arrays dentro de lumps.

---

# 49. Reinicio y shutdown

Probar:

- salir desde menu;
- error por WAD faltante;
- error por port wad faltante;
- error de video;
- varios inicios consecutivos.

Asegurar:

~~~text
Vita_SWDestroy
Vita_VideoShutdown
SDL_Quit
~~~

en orden coherente.

---

# 50. Criterios antes de habilitar resoluciones menores al usuario

No exponer selector hasta que funcionen:

- 960x544;
- 480x272;
- cambio de buffers;
- aspect ratio;
- screenshots;
- HUD;
- automap;
- menu;
- wipes.

Despues se pueden agregar:

~~~text
720x408
360x204
~~~

---

# 51. Arquitectura del launcher futuro

El launcher NO debe contener logica del engine.

Su salida puede ser simplemente una configuracion o argv:

~~~text
-iwad uma0:/data/DSDA-Doom/IWADs/doom2.wad
-vita-renderer software
-vita-resolution 480x272
-vita-filter nearest
~~~

Renderer futuro:

~~~text
-vita-renderer opengl
~~~

El ejecutable principal procesa la seleccion.

Evitar crear dos EBOOT distintos si no es necesario.

---

# 52. Fase futura: renderer OpenGL

Solo comenzar cuando la version software sea estable.

Orden recomendado:

1. volver a habilitar gl_* en Vita;
2. sustituir SDL_opengl/OpenGL::GL por VitaGL headers/libs;
3. portar gl_opengl.c;
4. eliminar GLU;
5. reemplazar tessellation GLU por libtess;
6. revisar shaders;
7. revisar FBO;
8. revisar VBO;
9. adaptar texture formats;
10. adaptar extensions;
11. probar gld_PreprocessLevel;
12. probar walls;
13. floors/ceilings;
14. sprites;
15. sky;
16. automap;
17. HUD;
18. wipes;
19. cambiar selector del futuro launcher.

Mantener el presentador software intacto durante todo ese trabajo.

---

# 53. Fases de commits recomendadas

## Commit 1 - Build skeleton

- Vita options;
- toolchain;
- packaging;
- OpenGL conditional.

## Commit 2 - Vita platform

- vita_system;
- paths;
- temp;
- logs.

## Commit 3 - IWAD auto discovery

- ux0/uma0/ur0;
- deterministic glob;
- -iwad override.

## Commit 4 - VitaGL init

- init/shutdown;
- native display.

## Commit 5 - software presenter

- 8-bit surface;
- RGBA texture;
- fullscreen quad;
- swap.

## Commit 6 - ARM safety

- unaligned reads;
- warnings.

## Commit 7 - controller

- diagnostics;
- mapping fixes if needed.

## Commit 8 - first playable

- MAP load;
- fixes;
- no audio.

## Commit 9 - SFX

## Commit 10 - music

## Commit 11 - resolution abstraction

- 960x544;
- 480x272 test;
- profiles table.

## Commit 12 - release cleanup

- remove diagnostic hacks;
- keep useful logging;
- VPK metadata.

---

# 54. Definicion de "port software completado"

Se puede considerar completada esta fase cuando:

- compila desde GitHub Actions con VitaSDK limpio;
- genera VPK instalable;
- encuentra IWAD automaticamente;
- soporta ux0, uma0 y ur0;
- usa el primer WAD de forma determinista;
- carga dsda-doom.wad interno;
- entra al menu;
- carga un mapa;
- renderiza 960x544 software correctamente;
- presenta mediante VitaGL;
- controller funciona;
- SFX funciona;
- musica base funciona o queda una limitacion documentada;
- save/load funciona;
- screenshots funcionan;
- ZIP/PWAD funciona;
- no depende del renderer OpenGL desktop;
- no depende de GLU;
- no depende de fork/exec;
- no tiene crashes ARM conocidos por alineacion;
- 480x272 puede activarse internamente como prueba;
- VitaGL continua a 960x544 independientemente de la resolucion interna;
- el codigo queda listo para recibir un launcher y un renderer OpenGL futuros.

---

# 55. Prioridad inmediata de desarrollo

Orden concreto para empezar a programar:

~~~text
1. CMake: separar DSDA OpenGL renderer
2. VitaSDK/VPK skeleton
3. vita_system.c
4. auto-IWAD
5. log
6. VitaGL init
7. VID_MODESW forzado
8. framebuffer 960x544
9. software -> VitaGL presentation
10. input
11. first menu
12. first map
13. ARM alignment fixes
14. performance instrumentation
15. audio
16. 480x272 internal test
~~~

No comenzar el renderer OpenGL hasta cerrar el punto 16.
