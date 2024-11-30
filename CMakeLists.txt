 # must first download openssl https://slproweb.com/products/Win32OpenSSL.html  Win64 OpenSSL v3.1.2 REGULAR MSI

cmake_minimum_required(VERSION 3.8)
project(Open62541_Over_ISOBUS)


###########
# Version #
###########

# The current version information. On the master branch, we take the version
# number from the latest release plus the "-undefined" label. Will be
# overwritten with more detailed information if git is available.
set(OPEN62541_VER_MAJOR 1)
set(OPEN62541_VER_MINOR 3)
set(OPEN62541_VER_PATCH 8)
set(OPEN62541_VER_LABEL "-undefined") # like "-rc1" or "-g4538abcd" or "-g4538abcd-dirty"
set(OPEN62541_VER_COMMIT "unknown-commit")
set(UA_ARCHITECTURES "win32")
option(UA_ENABLE_AMALGAMATION OFF)
option(UA_ENABLE_ENCRYPTION_OPENSSL ON)

include_directories("C:/Program Files (x86)/Kvaser/Canlib/INC")

include_directories("C:/Program Files/OpenSSL-Win64/include")
include_directories("C:/Program Files/OpenSSL-Win64/include/openssl")


include_directories("${CMAKE_SOURCE_DIR}/open62541/include")
include_directories("${CMAKE_SOURCE_DIR}/open62541/include/open62541")
include_directories("${CMAKE_SOURCE_DIR}/open62541/src/")
include_directories("${CMAKE_SOURCE_DIR}/open62541/src/client")
include_directories("${CMAKE_SOURCE_DIR}/open62541/src/server")
include_directories("${CMAKE_SOURCE_DIR}/open62541/src/pubsub")
include_directories("${CMAKE_SOURCE_DIR}/src_generated/")
include_directories("${CMAKE_SOURCE_DIR}/src_generated/open62541")
include_directories("${CMAKE_SOURCE_DIR}/open62541/plugins")
include_directories("${CMAKE_SOURCE_DIR}/open62541/plugins/include")
include_directories("${CMAKE_SOURCE_DIR}/open62541/plugins/include/open62541/plugin")
include_directories("${CMAKE_SOURCE_DIR}/open62541/plugins/include/crypto")
include_directories("${CMAKE_SOURCE_DIR}/open62541/plugins/include/crypto/openssl")
include_directories("${CMAKE_SOURCE_DIR}/open62541/arch")
include_directories("${CMAKE_SOURCE_DIR}/open62541/arch/win32")
include_directories("${CMAKE_SOURCE_DIR}/open62541/deps")

file(GLOB OPENSSL_FILES open62541/plugins/crypto/openssl/*.c)
file(GLOB SECURITY_FILES open62541/plugins/ua_config_default.c
open62541/plugins/ua_log_stdout.c
open62541/plugins/ua_log_syslog.c
open62541/plugins/ua_nodestore_hashmap.c
open62541/plugins/ua_nodestore_ziptree.c
open62541/plugins/ua_accesscontrol_default.c
)

file(GLOB NETWORK_PROTOCOL_FILES open62541/arch/*tcp.c)
file(GLOB WIN32_PROTOCOL_FILES "open62541/arch/win32/*.c")

file(GLOB GENERAL_PROTOCOL_FILES    open62541/src/*.c)
file(GLOB GENERATED_PROTOCOL_FILES  src_generated/open62541/*.c)
file(GLOB CLIENT_PROTOCOL_FILES     open62541/src/client/*.c)
file(GLOB SERVER_PROTOCOL_FILES     open62541/src/server/*.c)
file(GLOB PUBSUB_PROTOCOL_FILES     open62541/src/pubsub/*.c)

file(GLOB AMX_INFO_MODEL_FILES     amxModel.*)
message(status  "${NETWORK_PROTOCOL_FILES} value:")
message(status  ${NETWORK_PROTOCOL_FILES} )

link_directories("${CMAKE_SOURCE_DIR}/open62541/include")
link_directories("${CMAKE_SOURCE_DIR}/open62541/include/open62541")
link_directories("${CMAKE_SOURCE_DIR}/src_generated")
link_directories("${CMAKE_SOURCE_DIR}/src_generated/open62541")

find_package(OpenSSL REQUIRED)


add_executable(ClientCanNoEncryption	ClientCanNoEncryption.cpp	
open62541/plugins/ua_config_default.c
IsobusOpcUaLayer.c IsobusOpcUaLayer.h
IsobusCommon.c IsobusCommon.h
"C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h")

add_executable(ClientTcpNoEncryption ClientTcpNoEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusCommon.c IsobusCommon.h
"C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h")

add_executable(ClientCanEncryption ClientCanEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusOpcUaLayer.c IsobusOpcUaLayer.h
IsobusCommon.c IsobusCommon.h
"C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h")

add_executable(ClientTcpEncryption ClientTcpEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusCommon.c IsobusCommon.h
 "C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h" ) 
 

add_executable(ServerCanNoEncryption	ServerCanNoEncryption.cpp	
open62541/plugins/ua_config_default.c IsobusOpcUaLayer.c IsobusOpcUaLayer.h
IsobusCommon.c IsobusCommon.h
${AMX_INFO_MODEL_FILES}
"C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h")

add_executable(ServerTcpNoEncryption ServerTcpNoEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusCommon.c IsobusCommon.h
${AMX_INFO_MODEL_FILES}
 "C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h" )
 
add_executable(ServerCanEncryption ServerCanEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusCommon.c IsobusCommon.h
IsobusOpcUaLayer.c IsobusOpcUaLayer.h ${AMX_INFO_MODEL_FILES}
"C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h")

add_executable(ServerTcpEncryption ServerTcpEncryption.cpp 
${OPENSSL_FILES} ${SECURITY_FILES} open62541/plugins/ua_config_default.c
IsobusCommon.c IsobusCommon.h
${AMX_INFO_MODEL_FILES}
 "C:/Program Files (x86)/Kvaser/Canlib/INC/canlib.h" "IsobusTransportProtocol.h" )


target_link_libraries(ServerCanNoEncryption "C:/Program Files (x86)/Kvaser/Canlib/Lib/x64/canlib32.lib" OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ClientCanNoEncryption "C:/Program Files (x86)/Kvaser/Canlib/Lib/x64/canlib32.lib" OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ClientTcpEncryption OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ClientTcpNoEncryption OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ServerTcpEncryption OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ServerTcpNoEncryption OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)


target_link_libraries(ServerCanEncryption "C:/Program Files (x86)/Kvaser/Canlib/Lib/x64/canlib32.lib" OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)
target_link_libraries(ClientCanEncryption "C:/Program Files (x86)/Kvaser/Canlib/Lib/x64/canlib32.lib" OpenSSL::SSL ${CMAKE_SOURCE_DIR}/open62541.lib)

