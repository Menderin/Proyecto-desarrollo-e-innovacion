#!/usr/bin/env bash

set -u

IDF_POWERSHELL_PROFILE="C:/Espressif/tools/Microsoft.v5.5.4.PowerShell_profile.ps1"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MAIN_PROJECT_DIR="$SCRIPT_DIR"
CAM_PROJECT_DIR="$SCRIPT_DIR/experiments/esp32cam_camera_test"

# ESP-IDF en Windows instala un perfil PowerShell oficial. Git Bash/MSYS ya no
# esta soportado directamente por ESP-IDF v5.5, asi que este menu delega los
# comandos a PowerShell con el entorno cargado.
run_idf() {
    case "${OSTYPE:-}:${MSYSTEM:-}" in
        msys*:*|mingw*:*|cygwin*:*|*:MSYS*|*:MINGW*|*:UCRT*|*:CLANG*) ;;
        *)
            if command -v idf.py >/dev/null 2>&1; then
                idf.py "$@"
                return $?
            fi
            ;;
    esac

    if [ ! -f "$IDF_POWERSHELL_PROFILE" ]; then
        echo "Error: perfil PowerShell de ESP-IDF no encontrado:"
        echo "  $IDF_POWERSHELL_PROFILE"
        return 1
    fi

    if ! command -v powershell.exe >/dev/null 2>&1; then
        echo "Error: powershell.exe no fue encontrado."
        return 1
    fi

    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "\$ErrorActionPreference = 'Stop'; 'MSYSTEM','MSYSTEM_PREFIX','MINGW_PREFIX','MSYS2_PATH_TYPE','EXEPATH' | ForEach-Object { Remove-Item \"Env:\$_\" -ErrorAction SilentlyContinue }; . '$IDF_POWERSHELL_PROFILE' 6>\$null; idf.py @args" \
        "$@"
}

run_project_idf() {
    local project_dir="$1"
    shift

    if [ ! -f "$project_dir/CMakeLists.txt" ]; then
        echo "Error: proyecto ESP-IDF no encontrado:"
        echo "  $project_dir"
        return 1
    fi

    # Pasar la ruta explicitamente evita que PowerShell conserve el directorio
    # anterior cuando el menu se ejecuta desde Git Bash/MSYS.
    run_idf -C "$project_dir" "$@"
}

run_main_idf() {
    run_project_idf "$MAIN_PROJECT_DIR" "$@"
}

run_camera_idf() {
    run_project_idf "$CAM_PROJECT_DIR" "$@"
}

if ! run_idf --version </dev/null >/tmp/idf_menu_version.txt 2>/tmp/idf_menu_error.txt; then
    echo "==================================================================="
    echo "Error: 'idf.py' no fue encontrado en el sistema."
    echo "No se pudo cargar automaticamente el entorno de ESP-IDF desde:"
    echo "  $IDF_POWERSHELL_PROFILE"
    echo ""
    cat /tmp/idf_menu_error.txt
    echo "==================================================================="
    exit 1
fi

idf_version="$(tail -n 1 /tmp/idf_menu_version.txt)"
if [ -n "$idf_version" ]; then
    echo "ESP-IDF listo: $idf_version"
else
    echo "ESP-IDF listo: entorno cargado"
fi

show_menu() {
    echo "================================================="
    echo "       Herramientas ESP-IDF - Menu Rapido"
    echo "================================================="
    echo "1) Compilar (build) [COMPROBADA]"
    echo "2) Flashear (flash) [COMPROBADA]"
    echo "3) Mostrar monitor serial (monitor) [NO COMPROBADA]"
    echo "4) Compilar y Flashear (build flash) [COMPROBADA]"
    echo "5) Limpiar memoria Flash (erase-flash) [NO COMPROBADA]"
    echo "6) Compilar, Flashear y Monitor (build flash monitor) [NO COMPROBADA]"
    echo "7) Herramientas ESP32-CAM"
    echo "0) Salir"
    echo "================================================="
    echo -n "Seleccione una opcion [0-7]: "
}

show_camera_menu() {
    echo "================================================="
    echo "             Herramientas ESP32-CAM"
    echo "================================================="
    echo "1) Configurar Wi-Fi y Telegram (menuconfig)"
    echo "2) Compilar ESP32-CAM (build)"
    echo "3) Flashear ESP32-CAM (flash)"
    echo "4) Compilar y Flashear ESP32-CAM (build flash)"
    echo "5) Monitor serial ESP32-CAM (monitor)"
    echo "6) Compilar, Flashear y Monitor ESP32-CAM"
    echo "7) Limpiar Flash ESP32-CAM (erase-flash)"
    echo "0) Volver al menu principal"
    echo "================================================="
    echo -n "Seleccione una opcion [0-7]: "
}

camera_menu() {
    local camera_choice
    local serial_port

    while true; do
        show_camera_menu
        if ! read -r camera_choice; then
            return
        fi

        case $camera_choice in
            1)
                echo -e "\n---> ESP32-CAM: idf.py menuconfig\n"
                run_camera_idf menuconfig
                ;;
            2)
                echo -e "\n---> ESP32-CAM: idf.py build\n"
                run_camera_idf build
                ;;
            3)
                echo -e "\n---> ESP32-CAM: idf.py flash\n"
                run_camera_idf flash
                ;;
            4)
                echo -e "\n---> ESP32-CAM: idf.py build flash\n"
                run_camera_idf build flash
                ;;
            5)
                echo -n "Puerto serial ESP32-CAM [COM6]: "
                read -r serial_port
                serial_port="${serial_port:-COM6}"
                echo -e "\n---> ESP32-CAM: idf.py -p $serial_port monitor --no-reset\n"
                run_camera_idf -p "$serial_port" monitor --no-reset
                ;;
            6)
                echo -e "\n---> ESP32-CAM: idf.py build flash monitor\n"
                run_camera_idf build flash monitor
                ;;
            7)
                echo -e "\n---> ESP32-CAM: idf.py erase-flash\n"
                run_camera_idf erase-flash
                ;;
            0)
                return
                ;;
            *)
                echo -e "\nOpcion invalida. Seleccione un numero del 0 al 7.\n"
                continue
                ;;
        esac

        echo ""
        echo "Presione [Enter] para continuar..."
        read -r
    done
}

while true; do
    show_menu
    if ! read -r choice; then
        echo -e "\nSaliendo...\n"
        exit 0
    fi
    case $choice in
        1)
            echo -e "\n---> Ejecutando: idf.py build\n"
            run_main_idf build
            ;;
        2)
            echo -e "\n---> Ejecutando: idf.py flash\n"
            run_main_idf flash
            ;;
        3)
            echo -e "\n---> Ejecutando: idf.py monitor\n"
            # monitor se queda ejecutando hasta que presiones Ctrl+]
            run_main_idf monitor
            ;;
        4)
            echo -e "\n---> Ejecutando: idf.py build flash\n"
            run_main_idf build flash
            ;;
        5)
            echo -e "\n---> Ejecutando: idf.py erase-flash\n"
            run_main_idf erase-flash
            ;;
        6)
            echo -e "\n---> Ejecutando: idf.py build flash monitor\n"
            run_main_idf build flash monitor
            ;;
        7)
            camera_menu
            continue
            ;;
        0)
            echo -e "\nSaliendo...\n"
            exit 0
            ;;
        *)
            echo -e "\nOpcion invalida. Por favor seleccione un numero del 0 al 7.\n"
            ;;
    esac

    echo ""
    echo "Presione [Enter] para continuar..."
    read -r
done
