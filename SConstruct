import os

if hasattr(os,'uname'):
    system = os.uname()[0]
else:
    system = 'Windows'

env = Environment(ENV = os.environ,
                  CCFLAGS=['-Wall', '-g2'],
                  tools=['default'])

if os.environ.has_key('CC'):
    env.Replace(CC=os.environ['CC'])
if os.environ.has_key('CXX'):
    env.Replace(CXX=os.environ['CXX'])

driver = SConscript('driver/SConscript', exports='env')
