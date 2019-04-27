import sys
Import("env", "projenv")

# add     -fpermissive to CXX options
env.Append(CXXFLAGS = ['-fpermissive'])
projenv.Append(CXXFLAGS = ['-fpermissive'])

#print env.Dump()
#sys.exit(0)