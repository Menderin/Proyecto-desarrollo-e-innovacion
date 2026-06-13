#!/bin/bash

# Verificar si idf.py está disponible en el entorno actual
if ! command -v idf.py &> /dev/null
then
    echo "==================================================================="
    echo "Error: 'idf.py' no fue encontrado en el sistema."
    echo "Por favor, asegúrate de haber activado el entorno de ESP-IDF"
    echo "(usualmente ejecutando 'export.sh' o 'export.bat' / 'export.ps1')"
    echo "==================================================================="
    exit 1
fi

function show_menu {
    echo "================================================="
    echo "       Herramientas ESP-IDF - Menú Rápido"
    echo "================================================="
    echo "1) Compilar (build)"
    echo "2) Flashear (flash)"
    echo "3) Mostrar monitor serial (monitor)"
    echo "4) Compilar y Flashear (build flash)"
    echo "5) Limpiar memoria Flash (erase-flash)"
    echo "6) Compilar, Flashear y Monitor (build flash monitor)"
    echo "0) Salir"
    echo "================================================="
    echo -n "Seleccione una opción [0-6]: "
}

while true; do
    show_menu
    read choice
    case $choice in
        1)
            echo -e "\n---> Ejecutando: idf.py build\n"
            idf.py build
            ;;
        2)
            echo -e "\n---> Ejecutando: idf.py flash\n"
            idf.py flash
            ;;
        3)
            echo -e "\n---> Ejecutando: idf.py monitor\n"
            # monitor se queda ejecutando hasta que presiones Ctrl+]
            idf.py monitor
            ;;
        4)
            echo -e "\n---> Ejecutando: idf.py build flash\n"
            idf.py build flash
            ;;
        5)
            echo -e "\n---> Ejecutando: idf.py erase-flash\n"
            idf.py erase-flash
            ;;
        6)
            echo -e "\n---> Ejecutando: idf.py build flash monitor\n"
            idf.py build flash monitor
            ;;
        0)
            echo -e "\nSaliendo...\n"
            exit 0
            ;;
        *)
            echo -e "\n❌ Opción inválida. Por favor seleccione un número del 0 al 6.\n"
            ;;
    esac
    
    echo ""
    echo "Presione [Enter] para continuar..."
    read
done
