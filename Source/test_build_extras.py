import json, tempfile, unittest, zipfile
from pathlib import Path
from build_extras import files_in, embed, stage, clean_job

class ExtrasTests(unittest.TestCase):
    def test_seed_and_archive_packaged_without_touching_originals(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); save=root/'save'; save.mkdir(); (save/'progress.dat').write_bytes(b'progress')
            cheats=root/'cheats'; cheats.mkdir(); (cheats/'0123456789ABCDEF.txt').write_text('[Example]\n04000000 12345678 00000001\n')
            ipa=root/'game.ipa'
            with zipfile.ZipFile(ipa,'w') as z: z.writestr('Payload/Game.app/game',b'game')
            c={'target':'ios','saveImport':str(save),'cheatImport':str(cheats)}
            embed(ipa,c,'nxnative')
            with zipfile.ZipFile(ipa) as z:
                self.assertEqual(z.read('Payload/Game.app/game'),b'game')
                self.assertEqual(z.read('Payload/Game.app/NativeBridgeImport/save/progress.dat'),b'progress')
                self.assertEqual(json.loads(z.read('Payload/Game.app/NativeBridgeImport/manifest.json'))['saveDestination'],'save')
            self.assertEqual((save/'progress.dat').read_bytes(),b'progress')
    def test_symlink_save_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'link').symlink_to('/etc/hosts')
            with self.assertRaises(ValueError): files_in(root)
    def test_invalid_cheat_name_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'random.txt').write_text('[Example]')
            with self.assertRaises(ValueError): files_in(root,True)
    def test_cleanup_keeps_symlink_targets_and_unfinished_jobs(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); job=root/'jobs'/'123456abcdef'; job.mkdir(parents=True)
            original=root/'original'; original.write_text('keep'); (job/'input').symlink_to(original)
            with self.assertRaises(ValueError): clean_job(root,'123456abcdef')
            (job/'.nativebridge-complete').touch(); clean_job(root,'123456abcdef')
            self.assertEqual(original.read_text(),'keep'); self.assertFalse(job.exists())
    def test_cleanup_rejects_symlinked_job(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'jobs').mkdir(); outside=root/'outside'; outside.mkdir(); (outside/'.nativebridge-complete').touch()
            (root/'jobs'/'123456abcdef').symlink_to(outside)
            with self.assertRaises(ValueError): clean_job(root,'123456abcdef')
            self.assertTrue(outside.exists())
    def test_mush8_save_version(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); source=stage(root,{'saveVersion':'3.0.5'},'mush8')
            self.assertEqual(json.loads((source/'manifest.json').read_text())['saveDestination'],'work/save-3.0.5')

    def test_real_save_seed_function_preserves_progress(self):
        import subprocess, sys
        if sys.platform!='darwin': self.skipTest('Foundation test requires macOS')
        header=Path(__file__).with_name('iOS').joinpath('NativeBridgeSettings.h').read_text()
        function=header[header.index('static void NBImportSave(void) {'):header.index('static void NBApplyAspect(void) {')]
        function=function.replace('NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject','nbDocs').replace('NSBundle.mainBundle.resourcePath','nbResources')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); src=root/'test.m'; exe=root/'test'
            src.write_text('#import <Foundation/Foundation.h>\n#define NB_BACKEND 1\nstatic NSString *nbDocs,*nbResources;\nvoid runtime_account_uid(uint64_t out[2]) {out[0]=1;out[1]=2;}\n'+function+'\nint main(int argc,char **argv){@autoreleasepool {nbDocs=@(argv[1]);nbResources=@(argv[2]);NBImportSave();}return 0;}')
            subprocess.run(['xcrun','clang','-fobjc-arc','-framework','Foundation',str(src),'-o',str(exe)],check=True,capture_output=True)
            docs=root/'Documents'; docs.mkdir(); resources=root/'Resources'; resources.mkdir()
            save=root/'source';save.mkdir();(save/'progress.dat').write_text('imported')
            stage(resources,{'saveImport':str(save)},'macnative')
            subprocess.run([str(exe),str(docs),str(resources)],check=True,capture_output=True)
            destination=docs/'save/save/00000000000000020000000000000001/progress.dat'
            self.assertEqual(destination.read_text(),'imported')
            destination.write_text('new progress')
            subprocess.run([str(exe),str(docs),str(resources)],check=True,capture_output=True)
            self.assertEqual(destination.read_text(),'new progress')
