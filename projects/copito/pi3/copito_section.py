
# === Copito nomade: servicios Pi5 por el funnel con clave (2026-09-10) ===
# El llavero fuera de casa no ve la LAN: estas rutas proxyean Piper (voz) y
# cuentos del Pi5. Auth: cabecera X-Copito-Key (ver
# ~/.config/copito/funnel_key). Sin clave configurada = abierto (modo LAN).
import urllib.request as _curl_lib
from fastapi import Request as _FRequest

COPITO_PI5 = os.environ.get("COPITO_PI5", "100.66.50.66")
COPITO_KEYFILE = os.path.expanduser("~/.config/copito/funnel_key")
_COPITO_HITS = {}


def _copito_key():
    try:
        with open(COPITO_KEYFILE) as f:
            return f.read().strip()
    except OSError:
        return ""


def _copito_guard(request):
    import time as _t
    key = _copito_key()
    if key and request.headers.get("x-copito-key", "") != key:
        raise HTTPException(status_code=403, detail="falta clave")
    ip = request.client.host if request.client else "?"
    now = _t.time()
    lst = [t for t in _COPITO_HITS.get(ip, []) if now - t < 60]
    if len(lst) >= 120:
        raise HTTPException(status_code=429, detail="rate limit")
    lst.append(now)
    _COPITO_HITS[ip] = lst


async def _copito_proxy(request, target, guard=True):
    from fastapi.responses import Response as _Resp
    import asyncio
    if guard:
        _copito_guard(request)
    body = await request.body()
    if len(body) > 1024 * 1024:
        raise HTTPException(status_code=413, detail="body muy grande")
    ctype = request.headers.get("content-type", "application/json")

    def _do():
        req = _curl_lib.Request(target, data=body or None,
                                method=request.method,
                                headers={"Content-Type": ctype})
        # /story y /book generan en Ollama y estiran (minutos): 240;
        # voz/imagen/estático responden rápido: 90.
        _to = 240 if (target.rstrip("/").endswith(("/story", "/book"))
                      or "?topic=" in target or "?texto=" in target) else 90
        with _curl_lib.urlopen(req, timeout=_to) as resp:
            return (resp.status,
                    resp.getheader("Content-Type") or "application/octet-stream",
                    resp.read(1024 * 1024 + 1))

    try:
        code, ctype_out, data = await asyncio.to_thread(_do)
    except Exception as e:
        c = getattr(e, "code", None)
        if isinstance(c, int):
            raise HTTPException(status_code=c, detail="pi5 error")
        raise HTTPException(status_code=502, detail="pi5 sin respuesta")
    return _Resp(content=data, status_code=code, media_type=ctype_out)


@app.get("/copito/health")
async def copito_health(request: _FRequest):
    return {"ok": True, "key": bool(_copito_key()), "pi5": COPITO_PI5,
            "guard": "X-Copito-Key"}


@app.post("/copito/tts")
async def copito_tts(request: _FRequest):
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8881/tts")


@app.get("/copito/story")
async def copito_story(request: _FRequest):
    qs = str(request.url.query)
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/story?{qs}")


@app.get("/copito/story/img/{img_id}")
async def copito_story_img(img_id: str, request: _FRequest):
    if len(img_id) > 32 or not img_id.replace("_", "").replace("-", "").isalnum():
        raise HTTPException(status_code=400, detail="id invalido")
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/story/img/{img_id}")


@app.get("/copito/book")
async def copito_book(request: _FRequest):
    qs = str(request.url.query)
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book?{qs}")


@app.post("/copito/book")
async def copito_book_post(request: _FRequest):
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book")


def _copito_asset_id(v: str) -> bool:
    return len(v) <= 64 and v.replace("_", "").replace("-", "").isalnum()


@app.get("/copito/book/img/{img_id}")
async def copito_book_img(img_id: str, request: _FRequest):
    if not _copito_asset_id(img_id):
        raise HTTPException(status_code=400, detail="id invalido")
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book/img/{img_id}")


@app.get("/copito/book/jpg/{img_id}")
async def copito_book_jpg(img_id: str, request: _FRequest):
    if not _copito_asset_id(img_id):
        raise HTTPException(status_code=400, detail="id invalido")
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book/jpg/{img_id}")


@app.get("/copito/book/audio/{audio_id}")
async def copito_book_audio(audio_id: str, request: _FRequest):
    if not _copito_asset_id(audio_id):
        raise HTTPException(status_code=400, detail="id invalido")
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book/audio/{audio_id}")


@app.get("/copito/book/layout/{slug}")
async def copito_book_layout(slug: str, request: _FRequest):
    if not _copito_asset_id(slug):
        raise HTTPException(status_code=400, detail="id invalido")
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/book/layout/{slug}")


@app.get("/copito/demo")
@app.get("/copito/demo/")
async def copito_demo(request: _FRequest):
    return await _copito_proxy(request, f"http://{COPITO_PI5}:8890/demo")
