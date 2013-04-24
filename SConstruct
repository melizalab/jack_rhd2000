import os

if hasattr(os,'uname'):
    system = os.uname()[0]
else:
    system = 'Windows'

AddOption('--prefix',
          dest='prefix',
          type='string',
          nargs=1,
          action='store',
          metavar='DIR',
          default='/usr/local',
          help='installation prefix')

# if GetOption('jack')==None:
#     print "Must specify JACK 1 source directory with --jack"
#     Exit(2)


env = Environment(ENV = os.environ,
                  CCFLAGS=['-Wall', '-g2'],
                  CPPPATH=['/opt/local/include'],
                  PREFIX=GetOption('prefix'),
                  tools=['default'])

if os.environ.has_key('CC'):
    env.Replace(CC=os.environ['CC'])
if os.environ.has_key('CXX'):
    env.Replace(CXX=os.environ['CXX'])


SConscript('lib/SConscript', exports='env')
# SConscript('driver/SConscript', exports='env')
SConscript('test/SConscript', exports='env')
