# Библиотека для tools/*.sh: демо-проект редактора в run/demo.
# Проект ГЕНЕРИРУЕТСЯ (в git не коммитится, run/ в .gitignore).
# Использование: . "$(dirname "$0")/mkdemo.sh" && mkdemo "$ROOT"
mkdemo() {
    ROOT=$1
    PROJ=$ROOT/run/demo
    mkdir -p "$PROJ/depot"
    cat > "$PROJ/project.txt" <<'EOF'
name=Neon Voxel City
template=Voxel Terrain
backend=1
version=0.8.2
EOF
    cat > "$PROJ/scene.txt" <<'EOF'
e Hero -1 1 10.0000 20.0000 30.0000 0.1000 0.2000 0.3000 2.0000 2.0000 2.0000
e Sword 0 1 1.0000 2.0000 3.0000 0.0000 0.0000 0.0000 1.0000 1.0000 1.0000
EOF
    # Депо: импорт тайлов хешем движка (тот же fnv что в engine/asset.c:
    # оффсет 1469598103934665603 — НЕ стандартный FNV, сверяйся с движком).
    python3 - "$ROOT/engine/assets/tiles" "$PROJ/depot" <<'EOF'
import sys, os
tdir, depot = sys.argv[1], sys.argv[2]
def fnv(b):
    h = 1469598103934665603
    for x in b:
        h ^= x; h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h
names = ["grass_top.png", "dirt.png"]
idx = []
for n in names:
    d = open(os.path.join(tdir, n), "rb").read()
    h = "%016x" % fnv(d)
    stored = h + ".png"
    with open(os.path.join(depot, stored), "wb") as f:
        f.write(d)
    idx.append("%s %s %d %s\n" % (h, stored, len(d), n))
with open(os.path.join(depot, "index.txt"), "w") as f:
    f.writelines(idx)
EOF
    echo "$PROJ"
}
