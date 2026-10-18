#!/usr/bin/env python3
"""Voz en inglés con Kokoro-82M (Apache 2.0) vía kokoro-onnx, para demo.py.
Uso: tts_kokoro.py <carpeta del modelo> <voz> <salida.wav> <texto>
La carpeta tiene kokoro-v1.0.onnx y voices-v1.0.bin (releases de thewh1teagle/kokoro-onnx).
Voces: af_heart, am_michael (EE.UU.), bf_emma (Reino Unido)… Necesita espeak-ng
(ESPEAK_DATA_PATH apuntando a sus datos si el paquete no los encuentra)."""
import sys
import soundfile as sf
from kokoro_onnx import Kokoro

model, voice, out, text = sys.argv[1:5]
k = Kokoro(f'{model}/kokoro-v1.0.onnx', f'{model}/voices-v1.0.bin')
samples, sr = k.create(text, voice=voice, speed=1.0, lang='en-gb' if voice.startswith('b') else 'en-us')
sf.write(out, samples, sr)
