"""Compile public C models with the platform's native compiler."""
import os
from pathlib import Path
import shutil
import subprocess

def compile_model(sources,includes,output):
    output=Path(output)
    if os.name=='nt' and shutil.which('cl') and not os.environ.get('CC'):
        command=['cl','/nologo','/std:c11','/W4','/WX','/D_CRT_SECURE_NO_WARNINGS',
                 *['/I'+str(p) for p in includes],*[str(p) for p in sources],
                 '/Fe:'+str(output)]
    else:
        command=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                 *[arg for p in includes for arg in ('-I',str(p))],
                 *[str(p) for p in sources],'-lm','-o',str(output)]
    result=subprocess.run(command,cwd=output.parent,capture_output=True,text=True)
    if result.returncode:
        raise RuntimeError(result.stdout+'\n'+result.stderr)
    return output
