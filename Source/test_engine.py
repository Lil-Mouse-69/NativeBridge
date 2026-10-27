import json, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
import engine as e

class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.root=Path(self.tmp.name)
        self.games=self.root/'Game with spaces'; self.games.mkdir()
        self.keys=self.root/'prod.keys'; self.keys.write_text('master_key_00 = '+ '0'*32)
        self.c=dict(folder=str(self.games),keys=str(self.keys),output=str(self.root/'outputs'),backend='auto',target='mac',action='build',install=False,downloadsApproved=True)
    def tearDown(self): self.tmp.cleanup()
    def package(self,name):
        p=self.games/name; p.write_bytes(b'PFS0'+b'0'*20); return p
    def test_unknown_routes_to_metadata_capable_backend(self):
        self.package('unlabelled.nsp'); self.assertEqual(e.prepare(self.c)[1],'nxnative')
    def test_title_id_routes_zelda(self):
        self.package('[0100F2C0115B6000].nsp'); self.assertEqual(e.prepare(self.c)[1],'macnative')
    def test_ambiguous_base_stops(self):
        self.package('Tears of the Kingdom.nsp'); self.package('second base.nsp')
        with self.assertRaisesRegex(ValueError,'Multiple base'): e.prepare(self.c)
    def test_override_cannot_escape_directory(self):
        self.package('Tears of the Kingdom.nsp'); self.c['base']=str(self.keys)
        with self.assertRaisesRegex(ValueError,'inside'): e.prepare(self.c)
    def test_empty_keys_stops(self):
        self.package('game.xci'); self.keys.write_bytes(b'')
        with self.assertRaisesRegex(ValueError,'prod.keys'): e.prepare(self.c)
    def test_build_cache_packages_excluded(self):
        self.package('base.nsp'); cache=self.games/'0100_build'; cache.mkdir(); (cache/'stale.nsp').touch()
        self.assertEqual(len(e.scan(self.games)),1)
    def test_mush8_xci_rejected_with_alternative(self):
        self.package('Mario Kart.xci'); self.package('update.nsp')
        with self.assertRaisesRegex(ValueError,'nxnative'): e.prepare(self.c)
    def test_input_symlink_does_not_overwrite_regular_files(self):
        dest=self.root/'existing';dest.write_text('keep')
        with self.assertRaises(RuntimeError): e.link(self.keys,dest)
        self.assertEqual(dest.read_text(),'keep')
    def test_failed_child_propagates(self):
        with patch.object(e,'emit'):
            with self.assertRaisesRegex(RuntimeError,'code 7'): e.run(['/bin/sh','-c','exit 7'])
    def test_shell_metacharacters_are_literal(self):
        text='space; $(touch SHOULD_NOT_EXIST)'
        self.assertEqual(e.run(['/usr/bin/printf','%s',text],capture=True),text)
    def test_inspection_does_not_require_keys(self):
        self.package('game.nsp'); self.c.update(action='inspect',keys='')
        with patch.object(e,'emit') as emit:
            e.main(self.c)
        self.assertEqual(emit.call_args.args[0],'done')
    def test_nx_build_mock_integration(self):
        self.package('game.nsp'); repo=self.root/'repo';repo.mkdir(); state=self.root/'state';state.mkdir()
        calls=[]
        def fake_run(args,**kwargs):
            calls.append(list(map(str,args)))
            if '-o' in args: Path(args[args.index('-o')+1]).mkdir()
            return ''
        with patch.object(e,'ROOT',state),patch.object(e,'checkout',return_value=repo),patch.object(e,'run',side_effect=fake_run),patch.object(e,'tools_report',return_value={'ready':True}),patch.object(e,'emit'),patch.object(e.shutil,'which',return_value='/python3.14'):
            e.main(self.c)
        self.assertTrue((state/'history.json').exists())
        command=next(a for a in calls if '--platform' in a)
        self.assertEqual(command[command.index('--platform')+1],'mac-app')
        self.assertEqual(command[command.index('-k')+1],str(self.keys.resolve()))
    def test_missing_output_is_not_success(self):
        self.package('game.nsp'); repo=self.root/'repo';repo.mkdir(); state=self.root/'state';state.mkdir()
        with patch.object(e,'ROOT',state),patch.object(e,'checkout',return_value=repo),patch.object(e,'run',return_value=''),patch.object(e,'tools_report',return_value={'ready':True}),patch.object(e,'emit'),patch.object(e.shutil,'which',return_value='/python3.14'):
            with self.assertRaisesRegex(RuntimeError,'artifact is missing'): e.main(self.c)
        self.assertFalse((state/'history.json').exists())

class SetupTests(unittest.TestCase):
    def test_setup_without_consent_never_installs(self):
        report=dict(setupReady=True,missing=['metal'],disks=[{'ok':True,'path':'/'}])
        with patch.object(e,'tools_report',return_value=report),patch.object(e,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'approve'): e.install_missing({})
            run.assert_not_called()
    def test_new_dependency_requires_new_approval(self):
        report=dict(setupReady=True,missing=['metal','dotnet'],disks=[{'ok':True,'path':'/'}])
        with patch.object(e,'tools_report',return_value=report),patch.object(e,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'changed'): e.install_missing({'approved':['metal']})
            run.assert_not_called()
    def test_only_missing_approved_tools_installed(self):
        report=dict(setupReady=True,missing=['metal'],disks=[{'ok':True,'path':'/'}])
        with patch.object(e,'tools_report',side_effect=[report,dict(missing=[])]),patch.object(e,'run') as run,patch.object(e,'emit'):
            e.install_missing({'approved':['metal','dotnet']})
            run.assert_called_once_with(['xcodebuild','-downloadComponent','MetalToolchain'])
        e.WATCH_PATHS.clear()
    def test_failed_recheck_not_reported_as_success(self):
        report=dict(setupReady=True,missing=['metal'],disks=[{'ok':True,'path':'/'}])
        with patch.object(e,'tools_report',return_value=report),patch.object(e,'run'),patch.object(e,'emit'):
            with self.assertRaisesRegex(RuntimeError,'still missing'): e.install_missing({'approved':['metal']})
        e.WATCH_PATHS.clear()
    def test_shared_volume_requirements_are_added(self):
        with tempfile.TemporaryDirectory() as tmp:
            plan=e.disk_plan([(Path(tmp)/'cache',20*e.GIB,'cache'),(Path(tmp)/'output',30*e.GIB,'output')])
            self.assertEqual(len(plan),1)
            self.assertEqual(plan[0]['required'],60*e.GIB)
    def test_disk_shortfall_blocks_before_install(self):
        report=dict(setupReady=True,missing=['metal'],disks=[dict(ok=False,path='/',free=3*e.GIB,required=15*e.GIB,labels=['Metal'])])
        with patch.object(e,'tools_report',return_value=report),patch.object(e,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'Not enough'): e.install_missing({'approved':['metal']})
            run.assert_not_called()
    def test_build_requires_download_approval(self):
        with patch.object(e,'checkout') as checkout:
            with self.assertRaisesRegex(RuntimeError,'Approve'): e.main({'action':'build'})
            checkout.assert_not_called()
    def test_check_action_never_runs_mutating_commands(self):
        with patch.object(e,'tools_report',return_value={'ready':False}),patch.object(e,'run') as run,patch.object(e,'emit'):
            e.main({'action':'check'})
            run.assert_not_called()
    def test_invalid_container_does_not_pass_preflight(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); (root/'bad.nsp').write_bytes(b'not a container'); (root/'prod.keys').write_text('master_key_00 = '+'0'*32)
            r=e.game_report(dict(folder=tmp,keys=str(root/'prod.keys'),output=tmp,backend='nxnative'))
            self.assertFalse(r['ready'])
            self.assertTrue(any('Invalid' in i['detail'] for i in r['items']))
    def test_account_metadata_deduplicated_without_password(self):
        data=e.plistlib.dumps({'teams':[{'teamID':'ABCDE12345','teamName':'Personal Team','isFreeProvisioningTeam':True}]}).decode()
        with patch.object(e,'probe',side_effect=[(True,data),(False,'')]):
            result=e.accounts()
        self.assertEqual(result,[{'id':'ABCDE12345','name':'Personal Team','free':True}])
    def test_low_disk_monitor_kills_quiet_process(self):
        from collections import namedtuple
        usage=namedtuple('usage','total used free')
        e.WATCH_PATHS[:]=['/']
        try:
            with patch.object(e.shutil,'disk_usage',return_value=usage(100,99,1)),patch.object(e,'emit'):
                with self.assertRaisesRegex(RuntimeError,'emergency reserve'): e.run(['/bin/sleep','20'])
        finally: e.WATCH_PATHS.clear()

if __name__=='__main__': unittest.main()
