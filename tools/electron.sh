#!/bin/sh
# Electron УДАЛЁН из uitoolkit-ветки (свой in-engine UI; копия в engine+toolchain+electron).
# Вьюпорт: direct_view (GPU->GPU) или ui_test. Бывший лаунчер ниже — только история.
echo "electron удалён: UI живёт в engine/ui (ui_test), вьюпорт — direct_view" >&2
exit 1
