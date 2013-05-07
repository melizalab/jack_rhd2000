import os
from subprocess import Popen, PIPE

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
elif system == 'Linux':
    env.Append(LIBS=['dl'])
    # debian wheezy multiarch
    p = Popen("dpkg-architecture -qDEB_HOST_MULTIARCH", stdout=PIPE, stderr=PIPE, shell=True).stdout
    if p is not None: env.Append(LIBPATH=os.path.join('/usr/lib/',p.read().strip()))

SConscript('lib/SConscript', exports='env')
SConscript('driver/SConscript', exports='env')
SConscript('test/SConscript', exports='env')
