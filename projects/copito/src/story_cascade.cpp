// Cuento ilustrado en cascada: parte el cuento en 3-6 (JSON de Gemini),
// ilustra cada parte y la narra con voz de narrador. El cuento es SOLO
// imagen + voz (sin texto en pantalla): cada parte muestra su dibujo si la
// precarga lo tiene en ~4 s (gracia); si no, queda la imagen anterior.
// La narración nunca espera al dibujo.
// La tarea de precarga es dueña de TODOS sus buffers y del struct job: el
// reproductor solo pone abort=true al terminar y jamás libera ni toca el job
// después (2026-09-09: liberar desde la tarea = use-after-free = PANIC).
#include "story_cascade.h"
#include "ai_engine.h"
#include "epaper_driver.h"
#include "text_paginator.h"
#include "copito_log.h"
#include "copito_tools.h"
#include <new>

static const char *TAG = "CUENTO";

volatile bool g_storyCancel = false;
static volatile bool s_storyActive = false;
static volatile bool s_storyAdvance = false;
static volatile bool s_storyPaused = false;
static volatile uint32_t s_storyGen = 0;

extern bool g_textHold;
extern bool g_storyLoading;
extern TextPaginator paginator;

bool storyCascadeActive() { return s_storyActive; }
bool storyPaused() { return s_storyPaused; }
void storySetActive(bool on) { s_storyActive = on; }

// Espera el BOOT del usuario (el cuento lo arranca/avanza él, no solo):
// true si lo apretó, false si timeout/cancel. Consume el flag para que cada
// parte espere su propio BOOT.
bool storyWaitBoot(uint32_t gen, uint32_t timeoutMs) {
    unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (s_storyAdvance) {
            s_storyAdvance = false;
            return true;
        }
        if (gen != s_storyGen || g_storyCancel) return false;
        delay(50);
        copitoPokeActivity();
    }
    return false;
}

void storyRequestAdvance() {
    if (!s_storyActive) return;
    s_storyPaused = false; // avanzar retoma (por si estaba pausado por POWER)
    s_storyAdvance = true;
    g_ttsStopRequested = true; // corta la voz en curso (como BOOT normal)
}

void storyRequestPause() {
    if (!s_storyActive) return;
    CLOGI(TAG, "Cuento pausado en la imagen (BOOT avanza, POWER sale)");
    s_storyPaused = true;
    g_ttsStopRequested = true; // frena la voz ya, sin salir del cuento
}

void storyRequestExit() {
    if (!s_storyActive) return;
    CLOGI(TAG, "Salida del cuento pedida (PWR)");
    g_storyCancel = true;
    s_storyGen++; // la precarga en curso aborta y se autolimpia
    s_storyAdvance = true;
    g_ttsStopRequested = true;
}

// Trabajo de precarga: la tarea es dueña de job + bitmaps + prompts + ready
// y los libera SOLO ella al salir. El reproductor publica su fin con abort y
// después no toca el job nunca más. Los toques del reproductor (ready/bmp)
// van bajo s_storyJobLock; la liberación también. Sin esto, la tarea que
// termina rápido (todo 429) liberaba mientras el reproductor leía = PANIC.
struct StoryPrefetchJob {
    String *prompts;          // copia propia (n)
    String *imgUrls;          // bitmap cocinado Pi5 (n, "" = pipeline cloud)
    bool *imgFb;              // dibujo de respaldo offline (n, header X-Copito-Fallback)
    uint8_t n;
    uint32_t seedBase;
    uint32_t gen;
    uint8_t **bmp;            // bitmaps propios 5000 B (n)
    volatile bool *ready;     // parte lista para copiar
    volatile bool done;
    volatile bool abort;      // el reproductor termino: liberar todo y salir
};

// Gracia por parte para la imagen: si en este tiempo no está lista, la parte
// se cuenta igual (queda la imagen anterior). Con precarga total, la gracia
// casi siempre encuentra el dibujo ya listo.
static const unsigned long STORY_IMAGE_GRACE_MS = 4000;
// Espera máxima de precarga total (la animación de carga sigue mientras
// tanto). Al vencer, se arranca con lo que haya: el resto llega en fondo.
static const unsigned long STORY_PRELOAD_MS = 90000;
// Espera de BOOT por parte (el usuario avanza a su ritmo; al vencer sale).
static const unsigned long STORY_STEP_WAIT_MS = 180000;
// Espera máxima a que la precarga salga tras abort (suele ser instantánea;
// una descarga larga con cancel igual aborta por g_storyCancel).
static const unsigned long STORY_PREFETCH_JOIN_MS = 60000;

static SemaphoreHandle_t s_storyJobLock = NULL;

static void storyJobLockTake() {
    if (s_storyJobLock) xSemaphoreTake(s_storyJobLock, portMAX_DELAY);
}
static void storyJobLockGive() {
    if (s_storyJobLock) xSemaphoreGive(s_storyJobLock);
}

static void storyPrefetchTask(void *arg) {
    StoryPrefetchJob *job = (StoryPrefetchJob *)arg;
    // Dibuja una parte al bitmap dado: bitmap cocinado Pi5 si hay URL, si no
    // el pipeline cloud (Nano Banana -> Pollinations). Vacío total = texto.
    auto drawPart = [&](uint8_t i) -> bool {
        if (!job->bmp[i]) return false;
        if (job->imgUrls && job->imgUrls[i].length() > 0) {
            bool fb = false;
            if (aiEngine.downloadPiBitmap(job->imgUrls[i], job->bmp[i], &fb)) {
                storyJobLockTake();
                if (job->imgFb) job->imgFb[i] = fb;
                storyJobLockGive();
                return true;
            }
            CLOGW(TAG, "Precarga Pi5: parte %u/%u sin dibujo (ira con texto)", (unsigned)i + 1, (unsigned)job->n);
            return false;
        }
        if (job->prompts[i].length() == 0) return false;
        return aiEngine.fetchStoryBitmap(job->prompts[i], job->seedBase + (uint32_t)i * 7919, job->bmp[i]);
    };
    for (uint8_t i = 0; i < job->n; i++) {
        if (job->abort || job->gen != s_storyGen || g_storyCancel) break;
        if (drawPart(i)) {
            storyJobLockTake();
            job->ready[i] = true;
            storyJobLockGive();
            CLOGI(TAG, "Precarga: parte %u/%u lista", (unsigned)i + 1, (unsigned)job->n);
        } else if (job->imgUrls == nullptr || job->imgUrls[i].length() == 0) {
            if (job->prompts[i].length() > 0)
                CLOGW(TAG, "Precarga: parte %u/%u sin dibujo (ira con texto)", (unsigned)i + 1, (unsigned)job->n);
        }
        if (job->abort || job->gen != s_storyGen || g_storyCancel) break;
    }
    job->done = true;
    // Esperar la liberación del reproductor: SOLO abort la autoriza. Ni el
    // cambio de generación ni el cancel liberan acá: el reproductor puede
    // estar en un memcpy de microsegundos y hay que darle su tiempo.
    unsigned long w0 = millis();
    while (!job->abort && millis() - w0 < STORY_PREFETCH_JOIN_MS) delay(50);
    storyJobLockTake();
    if (job->bmp) {
        for (uint8_t i = 0; i < job->n; i++) {
            if (job->bmp[i]) free(job->bmp[i]);
        }
        free(job->bmp);
    }
    if (job->ready) free((void *)job->ready);
    if (job->imgFb) free(job->imgFb);
    if (job->prompts) delete[] job->prompts;
    if (job->imgUrls) delete[] job->imgUrls;
    storyJobLockGive();
    CLOGI(TAG, "Precarga terminada (buffers liberados por su duena)");
    delete job;
    vTaskDelete(NULL);
}

bool playStoryCascade(const String &topic) {
    if (topic.length() == 0) return false;
    uint32_t gen = ++s_storyGen;
    g_storyCancel = false;
    s_storyActive = true;
    s_storyAdvance = false;
    s_storyPaused = false;
    aiEngine.storyTtsResetPref(); // voz pegajosa: cada cuento redescubre
    copitoPokeActivity();

    // Pantalla de carga con rima + timer pixel a pixel (la anima el loop())
    g_storyLoading = true;
    epd.beginStoryLoading();

    StoryPart parts[STORY_PARTS_MAX];
    uint8_t n = 0;
    if (!aiEngine.fetchStoryParts(topic, parts, n)) {
        // Plan B sin cuota: el Pi5 arma el mismo JSON con Ollama local y ya
        // trae las imágenes dithereadas (imageUrl). Mismo reproductor.
        CLOGI(TAG, "Cascada cloud no disponible, probando Pi5 local...");
        if (!aiEngine.fetchPiStory(topic, parts, n)) {
            g_storyLoading = false;
            s_storyActive = false;
            return false; // plan C: cuento clasico
        }
        CLOGI(TAG, "Cuento Pi5 en marcha: %u partes ilustradas", (unsigned)n);
        // J5: juez de calidad (0 = sin nube para juzgar: se acepta). Con 1-2,
        // un intento más con variante de tópico; gana el mejor puntaje.
        {
            String full;
            for (uint8_t i = 0; i < n; i++) {
                full += parts[i].text;
                full += "\n";
                if (full.length() > 1500) break;
            }
            int score = aiEngine.aiJudgeStory(full);
            if (score > 0) CLOGI(TAG, "Juez Pi5: puntaje %d/5", score);
            if (score >= 1 && score <= 2) {
                StoryPart parts2[STORY_PARTS_MAX];
                uint8_t n2 = 0;
                CLOGI(TAG, "Juez Pi5: flojo (%d), pido otra versión...", score);
                if (aiEngine.fetchPiStory(topic + " (otra version)", parts2, n2) && n2 >= STORY_PARTS_MIN) {
                    String full2;
                    for (uint8_t i = 0; i < n2; i++) {
                        full2 += parts2[i].text;
                        full2 += "\n";
                        if (full2.length() > 1500) break;
                    }
                    int score2 = aiEngine.aiJudgeStory(full2);
                    CLOGI(TAG, "Juez Pi5 versión 2: puntaje %d/5", score2);
                    if (score2 > score) {
                        for (uint8_t i = 0; i < n2; i++) parts[i] = parts2[i];
                        n = n2;
                    }
                }
            }
        }
    }
    if (gen != s_storyGen || g_storyCancel) {
        g_storyLoading = false;
        s_storyActive = false;
        return true; // cancelado mientras generaba: manejado
    }

    // Buffer de pantalla propio (showBitmap lo copia al driver: reutilizable)
    uint8_t *dispBmp = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
    if (!dispBmp) {
        g_storyLoading = false;
        s_storyActive = false;
        return false;
    }

    uint32_t seedBase = (uint32_t)(millis() & 0x7FFFFFFF);
    if (!s_storyJobLock) s_storyJobLock = xSemaphoreCreateMutex();

    // La pantalla de carga cubrió solo el JSON (rápido): a partir de acá hay
    // texto propio en pantalla y la voz manda. La parte 1 también se cocina
    // en fondo: NADA bloquea el inicio de la narración.
    g_storyLoading = false;

    // Precarga de TODAS las partes en fondo (dueña de sus buffers: el
    // reproductor jamás libera ni toca el job tras abort=true).
    StoryPrefetchJob *job = new (std::nothrow) StoryPrefetchJob();
    if (job) {
        job->n = n;
        job->seedBase = seedBase;
        job->gen = gen;
        job->done = false;
        job->abort = false;
        job->prompts = new (std::nothrow) String[n];
        job->imgUrls = new (std::nothrow) String[n];
        job->bmp = (uint8_t **)calloc(n, sizeof(uint8_t *));
        job->ready = (volatile bool *)calloc(n, sizeof(bool));
        job->imgFb = (bool *)calloc(n, sizeof(bool));
        bool jobOk = (job->prompts && job->imgUrls && job->bmp && job->ready && job->imgFb);
        if (jobOk) {
            for (uint8_t i = 0; i < n; i++) {
                job->prompts[i] = parts[i].imagePrompt;
                job->imgUrls[i] = parts[i].imageUrl;
                job->bmp[i] = (uint8_t *)(psramFound() ? ps_malloc(EPD_BUFFER_SIZE) : malloc(EPD_BUFFER_SIZE));
                if (!job->bmp[i]) { jobOk = false; break; }
            }
        }
        if (jobOk) {
            BaseType_t r = xTaskCreate(storyPrefetchTask, "storyPre", 24576, job, 1, NULL);
            if (r != pdPASS) jobOk = false;
        }
        if (!jobOk) {
            // Sin precarga: cuento con texto (el cuento nunca se frena)
            CLOGW(TAG, "Sin tarea de precarga: cuento con texto");
            if (job->bmp) {
                for (uint8_t i = 0; i < n; i++) {
                    if (job->bmp[i]) free(job->bmp[i]);
                }
                free(job->bmp);
            }
            if (job->ready) free((void *)job->ready);
            if (job->imgFb) free(job->imgFb);
            if (job->prompts) delete[] job->prompts;
            if (job->imgUrls) delete[] job->imgUrls;
            delete job;
            job = NULL;
        }
    }

    // Reproductor: SOLO imagen + voz (2026-09-10: el cuento es todo imagen y
    // audio, sin páginas de texto mezcladas). Precarga total primero (la
    // barra carga TODO, nada se cuenta a medias); después cada parte espera
    // su BOOT: el usuario arranca y avanza a su ritmo. Sin dibujo, queda la
    // imagen anterior (o la carga si aún no hubo ninguna).
    // 0. Precarga total: todas listas, tarea terminada o timeout (con cancel).
    if (job) {
        unsigned long p0 = millis();
        while (millis() - p0 < STORY_PRELOAD_MS) {
            if (gen != s_storyGen || g_storyCancel) break;
            bool allReady = false;
            bool taskDone = false;
            storyJobLockTake();
            taskDone = job->done;
            allReady = true;
            for (uint8_t k = 0; k < job->n; k++) {
                if (!job->ready[k]) { allReady = false; break; }
            }
            storyJobLockGive();
            if (allReady || taskDone) break;
            delay(200);
            copitoPokeActivity();
        }
        CLOGI(TAG, "Precarga total lista (o timeout): a contar con BOOT");
    }
    g_storyLoading = false;
    // Primera imagen fija antes de que el usuario arranque (si ya llegó).
    if (job && n > 0) {
        bool img0 = false;
        storyJobLockTake();
        img0 = job->ready[0];
        if (img0) memcpy(dispBmp, job->bmp[0], EPD_BUFFER_SIZE);
        storyJobLockGive();
        if (img0 && gen == s_storyGen && !g_storyCancel) epd.showBitmap(dispBmp, true);
    }
    g_textHold = true;
    CLOGI(TAG, "Cuento listo: esperando BOOT para empezar");
    for (uint8_t i = 0; i < n; i++) {
        if (gen != s_storyGen || g_storyCancel) break;
        copitoPokeActivity();

        // 1. Cada parte espera su BOOT (el anterior ya se consumió).
        if (!storyWaitBoot(gen, STORY_STEP_WAIT_MS)) {
            CLOGI(TAG, "Cuento: sin BOOT, saliendo en parte %u/%u", (unsigned)i + 1, (unsigned)n);
            break;
        }
        if (gen != s_storyGen || g_storyCancel) break;

        // 2. Gracia para la imagen rezagada (rebanadas de 50ms).
        bool shown = false;
        unsigned long g0 = millis();
        while (!shown && millis() - g0 < STORY_IMAGE_GRACE_MS) {
            if (gen != s_storyGen || g_storyCancel || s_storyAdvance) break;
            bool imgReady = false;
            if (job) {
                storyJobLockTake();
                imgReady = job->ready[i];
                if (imgReady) memcpy(dispBmp, job->bmp[i], EPD_BUFFER_SIZE);
                storyJobLockGive();
            }
            if (imgReady) {
                epd.showBitmap(dispBmp, true);
                shown = true;
            } else {
                delay(50);
                copitoPokeActivity();
            }
        }
        g_textHold = true; // retener dibujo (o carga) toda la narración
        if (gen != s_storyGen || g_storyCancel) break;
        if (s_storyAdvance) continue; // BOOT en la gracia = siguiente parte

        CLOGI(TAG, "Contando parte %u/%u (%u letras)%s%s", (unsigned)i + 1, (unsigned)n,
              (unsigned)parts[i].text.length(), shown ? " con dibujo" : " con texto",
              (shown && job && job->imgFb && job->imgFb[i]) ? " (respaldo offline)" : "");
        aiEngine.narrateStoryPart(parts[i].text, nullptr);
        if (gen != s_storyGen || g_storyCancel) break;
        // Pausado por 1er POWER: queda en la imagen hasta BOOT (avanza) o
        // 2do POWER (sale). Sin busy-wait agresivo: rebanadas de 120ms.
        while (s_storyPaused && !s_storyAdvance && gen == s_storyGen && !g_storyCancel) {
            delay(120);
            copitoPokeActivity();
        }
        if (gen != s_storyGen || g_storyCancel) break;
        if (i + 1 >= n) break;
        // Sin avance automático: la parte siguiente espera su propio BOOT.
    }

    free(dispBmp);
    // Liberación del reproductor: SOLO publica abort y suelta el puntero.
    // La tarea libera todo y se borra sola; tocar el job acá = UAF = PANIC.
    if (job) job->abort = true;
    job = NULL;
    g_storyLoading = false;
    s_storyActive = false;
    CLOGI(TAG, "Cuento terminado (%u partes)", (unsigned)n);
    return true;
}
