idf_component_register(SRCS "main.c" "ota.c" "discovery.c" "wifi_config.c"
                  INCLUDE_DIRS "."
                  REQUIRES driver esp_timer nvs_flash esp_wifi esp_netif esp_http_server esp_app_format app_update)

# -------------------------------------------------------------------------
# Build flags: TARGET_DEVICE (torrent|switchback) and DEVICE_INSTANCE (0-2)
# Usage: idf.py build -DTARGET_DEVICE=switchback -DDEVICE_INSTANCE=1
# -------------------------------------------------------------------------

if(NOT DEFINED TARGET_DEVICE)
    set(TARGET_DEVICE "torrent")
endif()

if(NOT DEFINED DEVICE_INSTANCE)
    set(DEVICE_INSTANCE 0)
endif()

# Validate instance range
if(DEVICE_INSTANCE LESS 0 OR DEVICE_INSTANCE GREATER 2)
    message(FATAL_ERROR "DEVICE_INSTANCE must be 0, 1, or 2 (got: ${DEVICE_INSTANCE})")
endif()

# Map device type to CAN ID bases and compile definitions
if(TARGET_DEVICE STREQUAL "torrent")
    set(CAN_ID_TOGGLE_BASE 0x18)
    set(CAN_ID_STATUS_BASE 0x1B)
    set(DEVICE_TYPE_DEFINE "TARGET_DEVICE_TORRENT")
elseif(TARGET_DEVICE STREQUAL "switchback")
    set(CAN_ID_TOGGLE_BASE 0x25)
    set(CAN_ID_STATUS_BASE 0x28)
    set(DEVICE_TYPE_DEFINE "TARGET_DEVICE_SWITCHBACK")
else()
    message(FATAL_ERROR "TARGET_DEVICE must be 'torrent' or 'switchback' (got: '${TARGET_DEVICE}')")
endif()

message(STATUS "Tapper target: ${TARGET_DEVICE}, instance: ${DEVICE_INSTANCE}, toggle_base=${CAN_ID_TOGGLE_BASE}, status_base=${CAN_ID_STATUS_BASE}")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MODULE_TYPE="tapper"
    ${DEVICE_TYPE_DEFINE}
    DEVICE_INSTANCE=${DEVICE_INSTANCE}
    CAN_ID_TOGGLE_BASE=${CAN_ID_TOGGLE_BASE}
    CAN_ID_STATUS_BASE=${CAN_ID_STATUS_BASE}
    TARGET_DEVICE_NAME="${TARGET_DEVICE}"
)
