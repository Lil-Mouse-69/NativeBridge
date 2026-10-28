"""Execute the BOTW startup patch against synthetic module memory, never game data."""
import shutil, subprocess, tempfile, unittest
from pathlib import Path

HARNESS=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t *base; uint64_t file_size, size; } Module;
static int CODE_PATCHED_AT_BUILD=0;
static int ios_code(void) { return 1; }
static void runtime_fail(const char *s) { fprintf(stderr,"%s\n",s); exit(7); }
static uint32_t adrp_reg(unsigned rd,uint64_t site,uint64_t target) {
 int64_t pages=(int64_t)(target>>12)-(int64_t)(site>>12);
 return 0x90000000u|((pages&3)<<29)|(((pages>>2)&0x7ffff)<<5)|rd;
}
static void put_words(Module *m,uint64_t off,const uint32_t *words,size_t n,const char *what) {
 if(CODE_PATCHED_AT_BUILD) { if(memcmp(m->base+off,words,4*n)) runtime_fail(what); }
 else memcpy(m->base+off,words,4*n);
}
#include "BOTWPhoneResolution.c.inc"
int main(int argc,char **argv) {
 Module m={0}; m.file_size=PHONE_RENDER+0x1000;
 uint64_t data=(m.file_size+0x3fff)&~0x3fffULL; m.size=data+0x4000;
 if(posix_memalign((void**)&m.base,0x4000,m.size)) return 2;
 memset(m.base,0,m.size);
 uint32_t original[]={0xd2a89408u,0xf2e89408u,PHONE_RENDER_STR};
 memcpy(m.base+PHONE_RENDER,original,sizeof original);
 if(argc>1) m.base[PHONE_RENDER]=0;
 patch_phone_render(&m);
 uint32_t patched[3];memcpy(patched,m.base+PHONE_RENDER,sizeof patched);
 if(patched[1]!=0xf9401508u || patched[2]!=PHONE_RENDER_STR) return 3;
 CODE_PATCHED_AT_BUILD=1;
 const char *choices[]={"960x540","1280x720","default"};
 for(unsigned i=0;i<3;i++) {
   setenv("TOTK_RENDER_RES",choices[i],1); patch_phone_render(&m);
   float sizes[2];memcpy(sizes,m.base+data+40,sizeof sizes);
   if(sizes[0]!=1280 || sizes[1]!=(i==1?1280:960)) return 4;
   if(memcmp(patched,m.base+PHONE_RENDER,sizeof patched)) return 5;
 }
 free(m.base);return 0;
}
'''
@unittest.skipUnless(shutil.which('clang'),'C compiler required for patch execution tests')
class PhoneResolutionTests(unittest.TestCase):
    def test_both_botw_versions_switch_data_without_runtime_code_writes(self):
        for site,store in ((0x18216c8,0xf9094b88),(0x1266114,0xf9090a68)):
            with self.subTest(site=site), tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); source=root/'test.c';source.write_text(HARNESS);exe=root/'test'
                subprocess.run(['clang','-std=c11','-D_POSIX_C_SOURCE=200809L',f'-DPHONE_RENDER={site}',f'-DPHONE_RENDER_STR={store}', '-I',str(Path(__file__).parent/'iOS'),str(source),'-o',str(exe)],check=True,capture_output=True)
                good=subprocess.run([str(exe)],capture_output=True)
                self.assertEqual(good.returncode,0,good.stderr.decode())
                bad=subprocess.run([str(exe),'bad-original'],capture_output=True)
                self.assertEqual(bad.returncode,7)

if __name__=='__main__':unittest.main()
