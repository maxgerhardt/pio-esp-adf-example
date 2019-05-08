# NONE OF THIS SHIT ACTUALLY WORKS
# have to open an issue for it
# https://github.com/platformio/platformio-core/issues/2431

#Import("env")
import os

#print("=========== FIX_COMPILATION CALLED =========")

# directory in which this script is located
#script_dir = os.path.dirname(os.path.realpath('__file__'))
#global_env = DefaultEnvironment()
#global_env.Append(CPPPATH=[
#    os.path.join(script_dir, "include"),
#    os.path.join(script_dir, "lib", "include")
#])

#env.Append(CPPPATH=[
##    os.path.join(script_dir, "include"),
#    os.path.join(script_dir, "lib", "include")
#])

# block esp_http_client from ESP-IDF being compiled
# original value is -<.git/> -<svn/> -<example/> -<examples/> -<test/> -<tests/>
#env.Append(SRC_FILTER=["-<components/esp_http_client/>"])
#global_env.Append(SRC_FILTER=["-<components/esp_http_client/>"])

#print global_env.Dump()
