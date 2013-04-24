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

env = Environment(ENV = os.environ,
                  CCFLAGS=['-Wall', '-g2'],
                  PREFIX=GetOption('prefix'),
                  tools=['default'])

if os.environ.has_key('CC'):
    env.Replace(CC=os.environ['CC'])
if os.environ.has_key('CXX'):
    env.Replace(CXX=os.environ['CXX'])

if system == 'Darwin':
    env.Append(CXXFLAGS=["-mmacosx-version-min=10.4"],
               CPPPATH=['/opt/local/include'],
               LIBPATH=['/opt/local/lib'])


SConscript('lib/SConscript', exports='env')
SConscript('driver/SConscript', exports='env')
SConscript('test/SConscript', exports='env')
