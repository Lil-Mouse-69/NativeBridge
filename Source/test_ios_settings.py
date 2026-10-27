"""Integration tests against downloaded upstream source snapshots, when provided.
NATIVEBRIDGE_UPSTREAM_FIXTURES points to the snapshot directory used for validation.
"""
import os, shutil, tempfile, unittest
from pathlib import Path
import ios_settings as patcher

FIXTURES=Path(os.environ.get('NATIVEBRIDGE_UPSTREAM_FIXTURES','/not-provided'))
MAPPING={
    'nxnative':{'nx-main.m':'dm/ios/Sources/main.m','nx-front_ios.m':'dm/ios/Sources/front_ios.m'},
    'macnative':{'mac-main.m':'games/totk/ios/Sources/main.m','mac-build_ipa.py':'games/totk/ios/tools/build_ipa.py'},
    'mush8':{'mush-build.rs':'crates/nxhost/build.rs','mush-main.rs':'crates/nxhost/src/main.rs'}
}
@unittest.skipUnless(FIXTURES.is_dir(),'Set NATIVEBRIDGE_UPSTREAM_FIXTURES to actual upstream snapshots')
class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()
    def fixture(self,backend):
        root=self.root/backend
        for src,dest in MAPPING[backend].items():
            path=root/dest;path.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(FIXTURES/src,path)
        return root
    def test_all_three_real_backends_patch_and_are_idempotent(self):
        for backend in MAPPING:
            with self.subTest(backend=backend):
                root=self.fixture(backend);patcher.apply(root,backend)
                before={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
                patcher.apply(root,backend)
                after={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
                self.assertEqual(before,after)
                self.assertTrue((root/'.nativebridge-settings.json').is_file())
    def test_owned_patch_upgrade_uses_verified_originals(self):
        import json
        root=self.fixture('nxnative');patcher.apply(root,'nxnative')
        manifest=root/'.nativebridge-settings.json';data=json.loads(manifest.read_text());data['revision']='older-version';manifest.write_text(json.dumps(data))
        patcher.apply(root,'nxnative')
        self.assertNotEqual(json.loads(manifest.read_text())['revision'],'older-version')
        self.assertEqual((root/'dm/ios/Sources/main.m').read_text().count('#define NB_BACKEND 0'),1)

    def test_upstream_drift_writes_nothing(self):
        root=self.fixture('nxnative'); p=root/'dm/ios/Sources/front_ios.m';p.write_text(p.read_text().replace('- (void)startGame {','- (void)startRenamedGame {'))
        before={p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()}
        with self.assertRaisesRegex(RuntimeError,'no longer matches'): patcher.apply(root,'nxnative')
        self.assertEqual(before,{p.relative_to(root):p.read_bytes() for p in root.rglob('*') if p.is_file()})
    def test_local_edits_to_patched_source_are_not_overwritten(self):
        root=self.fixture('mush8');patcher.apply(root,'mush8');p=root/'crates/nxhost/src/main.rs';p.write_text(p.read_text()+'\n// custom edit\n')
        with self.assertRaisesRegex(RuntimeError,'source changed'):patcher.apply(root,'mush8')
        self.assertTrue(p.read_text().endswith('// custom edit\n'))
    def test_zeldas_export_hook_is_version_scoped(self):
        root=self.fixture('macnative');patcher.apply(root,'macnative')
        source=(root/'games/totk/ios/tools/build_ipa.py').read_text()
        compile(source,'build_ipa.py','exec')
        self.assertIn('if a.version == "v143": e["TOTK_RENDER_RES"] = "1280x720"',source)
    def test_original_sources_backed_up_exactly(self):
        root=self.fixture('nxnative');patcher.apply(root,'nxnative')
        for src,dest in MAPPING['nxnative'].items():self.assertEqual((FIXTURES/src).read_bytes(),(root/'.nativebridge-originals'/dest).read_bytes())

class AnchorTests(unittest.TestCase):
    def test_repeated_or_missing_anchors_rejected(self):
        for text in ['none','hook hook']:
            with self.assertRaises(RuntimeError):patcher.once(text,'hook','replacement')
if __name__=='__main__':unittest.main()
