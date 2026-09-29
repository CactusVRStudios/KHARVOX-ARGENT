"""Regression cases for CPU submission labels and optional FOV experiments."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('analysis', Path(__file__).resolve().parents[1]/'tools/analyze_performance_diagnostics.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class AnalysisTests(unittest.TestCase):
    def analyze(self, text):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)/'quad.log'
            path.write_text(text, encoding='utf-8')
            return analysis.analyze(path)

    def test_submission_group_straddles_xr(self):
        # Real GPU ticks exceed IEEE double's exact integer range.
        anchor = 1790164103935391680
        def command(start, end):
            return f'0 PERF_GPU_COMMAND submitFrame=16 queue=1 family=0 submits=1 startTick={anchor+start} endTick={anchor+end} regionSamples=0 regionsSeen=0\n'
        result = self.analyze('0 PERF_GPU_SUPPORT family=0 periodNs=1 validBits=64\n'+
                              command(0, 1000000)+command(3000000, 4000000)+
                              f'0 PERF_GPU_XR serial=16 queue=1 context=world startTick={anchor+2000000} endTick={anchor+2100000} barriersMs=0 copyFsrMs=.1 handsMs=0 totalMs=.1 fenceWaitMs=5\n')
        group = result['sampledSubmissionGroups'][0]
        self.assertEqual(group['observedEnvelopeMs'], 4)
        self.assertEqual(group['commandsBeforeXr'], 1)
        self.assertEqual(group['commandsAfterXrStart'], 1)
        self.assertFalse(group['isGameFrameGpuTime'])
        self.assertNotIn('precedingSamplesToXrStartMs', group)
        self.assertNotIn('sampledGameQueues', result)

    def test_fov_phase_edges_and_record_frame_deduplication(self):
        rows = []
        for phase, actual, start in [(1, 148, 0), (2, 120, 10000000)]:
            rows.append(f'0 FRAME_TIMELINE recordsUs=20000/8000/12000/G,16000/7000/9000/G perfModes=1,1 entriesUs={start},{start+3000000} fovStates={phase}/{actual},{phase}/{actual}\n')
        for ids, draws in [('16,32', 100), ('32,48', 200)]:
            rows.append(f'0 PERF_FOV_COMMANDS mode=1 phase=2 actual=120 recordFrames={ids} draws={draws} indirectDrawCalls=0 dispatches=0 sets=0 vertices=0 index=0 push=0 pipelineBinds=0\n')
        result = self.analyze(''.join(rows))
        self.assertEqual(len(result['fovComparison']), 2)
        self.assertTrue(all(row['intervalMs']['n'] == 1 and row['intervalMs']['mean'] == 16 for row in result['fovComparison']))
        self.assertEqual(result['fovRecordedCommands'][0]['sampledRecordFrames'], 3)
        self.assertEqual(result['fovRecordedCommands'][0]['drawsPerSampledRecordFrame'], 100)


if __name__ == '__main__':
    unittest.main()
