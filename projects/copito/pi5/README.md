# MUDADO → Cuentos-Backend (`Documents/Github/Cuentos-Backend`)
#
# El servicio de cuentos (`copito_story.py`, `evaluar_cuentos.py` y la demo web)
# ahora vive en su propio proyecto. Este directorio queda como referencia.
#
# - Servicio y deploy Pi5: `../Cuentos-Backend` (ver `README.md` y `docs/DEPLOY_PI5.md`)
# - Contrato congelado con este firmware: `../Cuentos-Backend/CONTRACT_ESP32.md`
#   (`GET <pi>:8890/story?topic=` + `/story/img/<id>` de 5000 bytes — no cambiar
#   esos paths sin versionar; el test guardián es `tests/test_esp32_contract.py`)
# - Front: `../Cuentos-Front` (Vite + TS; `legacy/demo.html` es la demo original)
#
# El firmware no cambió: `src/ai_engine.cpp` (`fetchPiStory`, `STORY_PI_PORT 8890`,
# host `cfg.ollama_host_pi`) sigue hablando contra el mismo puerto y formato.
