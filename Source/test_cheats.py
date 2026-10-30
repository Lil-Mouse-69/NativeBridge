import subprocess,sys,tempfile,unittest
from pathlib import Path

@unittest.skipUnless(sys.platform=='darwin','Requires Foundation')
class CheatExecutionTests(unittest.TestCase):
    def test_parser_executes_only_validated_data_writes(self):
        header=Path(__file__).with_name('iOS').joinpath('NativeBridgeCheats.h').resolve()
        code='''#import <Foundation/Foundation.h>
#include <stdint.h>
#include <string.h>
#import "HEADER"
int main(void) { @autoreleasepool {
 uint8_t main[256]={0}; nb_cheats_register(main,128,sizeof main);
 NSString *failure=nil;
 NSString *bid=@"0123456789ABCDEF";
 [bid writeToFile:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"nativebridge-build-id.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
 [NSUserDefaults.standardUserDefaults setObject:bid forKey:@"NativeBridge.cheatBuildID"];
 NSArray *groups=NBParseCheats(@"[Test]\\n04000000 00000080 DEADBEEF\\n",&failure);
 if(!groups || failure || NBCheckCheatWrites(groups)) return 1;
 NSMutableDictionary *enabled=[groups[0] mutableCopy]; enabled[@"enabled"]=@YES;
 [NSUserDefaults.standardUserDefaults setObject:@[enabled] forKey:NBCheatsKey]; NBCheatTick();
 uint32_t value; memcpy(&value,main+128,4); if(value!=0xDEADBEEF) return 2;
 enabled[@"enabled"]=@NO; [NSUserDefaults.standardUserDefaults setObject:@[enabled] forKey:NBCheatsKey]; memset(main+128,0,4); NBCheatTick();
 memcpy(&value,main+128,4); if(value) return 3;
 NSArray *code=NBParseCheats(@"[Code]\\n04000000 00000004 DEADBEEF\\n",&failure); if(!NBCheckCheatWrites(code)) return 4;
 NSArray *overflow=NBParseCheats(@"[Overflow]\\n08000000 000000FC 00000000 00000001\\n",&failure); if(!NBCheckCheatWrites(overflow)) return 5;
 if(NBParseCheats(@"[Pointer]\\n580F0000 00000080\\n",&failure)) return 6;
 NSArray *wide=NBParseCheats(@"[Wide]\\n08000000 00000088 01234567 89ABCDEF\\n",&failure); if(!wide || [wide[0][@"writes"][0][@"value"] unsignedLongLongValue]!=0x0123456789ABCDEFULL) return 7;
 enabled[@"enabled"]=@YES; [NSUserDefaults.standardUserDefaults setObject:@[enabled] forKey:NBCheatsKey];
 [NSUserDefaults.standardUserDefaults setObject:@"DIFFERENT-BUILD" forKey:@"NativeBridge.cheatBuildID"]; NBCheatTick();
 memcpy(&value,main+128,4); if(value) return 8;
 [NSUserDefaults.standardUserDefaults removeObjectForKey:NBCheatsKey];
 [NSUserDefaults.standardUserDefaults removeObjectForKey:@"NativeBridge.cheatBuildID"];
 } return 0; }
'''.replace('HEADER',str(header))
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.m'; src.write_text(code); exe=Path(d)/'test'
            subprocess.run(['xcrun','clang','-fobjc-arc','-framework','Foundation',str(src),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)
