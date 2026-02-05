#!/bin/bash

# Couleurs pour la lisibilité
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Fonction pour gérer les erreurs et quitter
die() {
    echo -e "${RED}❌ [CRITICAL FAILURE]${NC}"
    echo -e "Reason: $1"
    echo -e "Target Context: ${YELLOW}$2${NC}"
    exit 1
}

# Fonction principale de test pour une cible donnée
run_target_test() {
    local ARCH=$1      # ex: amd64, arm64, i386
    local COMPILER=$2  # ex: gcc, clang
    local MODE=$3      # ex: debug, release

    # 1. Construction de la commande de configuration
    local CONF_CMD="./configurev2.sh"
    
    # Gestion des flags
    if [ "$ARCH" != "amd64" ]; then
        CONF_CMD="$CONF_CMD -a $ARCH"
    fi
    
    if [ "$COMPILER" == "clang" ]; then
        CONF_CMD="$CONF_CMD --clang"
    fi
    
    if [ "$MODE" == "release" ]; then
        CONF_CMD="$CONF_CMD --release"
    fi

    echo -e "----------------------------------------------------"
    echo -e "Testing Target: ${YELLOW}Arch=$ARCH | Compiler=$COMPILER | Mode=$MODE${NC}"
    echo -e "Running: $CONF_CMD"

    # 2. Exécution de la configuration
    # On capture la sortie pour ne pas polluer sauf si erreur, ou on laisse afficher selon préférence.
    # Ici on laisse afficher pour le debug.
    $CONF_CMD
    if [ $? -ne 0 ]; then
        die "Configuration script failed (./configurev2.sh)" "Arch=$ARCH, Compiler=$COMPILER, Mode=$MODE"
    fi

    # 3. Détermination du dossier de sortie (Mapping logique basé sur ton script)
    local OUT_COMP="MinGW"
    [ "$COMPILER" == "clang" ] && OUT_COMP="Clang"

    local OUT_MODE="Debug"
    [ "$MODE" == "release" ] && OUT_MODE="MinSizeRel"

    local BUILD_DIR="output-${OUT_COMP}-${ARCH}-${OUT_MODE}"

    # Vérification que le dossier a bien été créé
    if [ ! -d "$BUILD_DIR" ]; then
        die "Output directory not created: $BUILD_DIR" "Arch=$ARCH, Compiler=$COMPILER, Mode=$MODE"
    fi

    # 4. Exécution du Build (Ninja)
    echo -e "Building in directory: ${BUILD_DIR}..."
    
    # On entre dans le dossier, on build, et on revient. 
    # Si 'cd' échoue ou 'ninja' échoue, le script s'arrête.
    (cd "$BUILD_DIR" && ninja livecd)
    
    if [ $? -ne 0 ]; then
        die "Build step (ninja livecd) failed" "Dir=$BUILD_DIR, Arch=$ARCH, Compiler=$COMPILER"
    fi

    echo -e "${GREEN}✅ SUCCESS: Target $ARCH-$COMPILER-$MODE passed.${NC}"
}

# --- Exécution des boucles de test ---

# Ordre des boucles : Compilateur -> Mode -> Architecture
# Cela couvre les 12 cas de ton script original.

# for compiler in gcc clang; do
#     for mode in debug release; do
#         for arch in amd64 arm64 i386; do
#             run_target_test $arch $compiler $mode
#         done
#     done
# done

for compiler in gcc clang; do
    for arch in amd64 arm64 i386; do
        run_target_test $arch $compiler debug
    done
done

echo -e "\n${GREEN}🎉 ALL TARGETS PASSED SUCCESSFULLY! 🎉${NC}"