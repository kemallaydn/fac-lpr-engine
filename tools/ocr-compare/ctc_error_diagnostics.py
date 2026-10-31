#!/usr/bin/env python3
"""CTC ground-truth Viterbi analysis of actual model logits. OFFLINE ONLY.

Never used as an inference decoder or a production decision rule.
Softmax outputs and CTC beam scores are not calibrated correctness rates.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import numpy as np

def load_contract(path):
    values = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.lstrip().startswith("#"):
            k, sep, v = line.partition("=")
            if not sep:
                raise ValueError("Invalid OCR contract")
            values[k.strip()] = v.strip()
    charset = values["ocr.charset"]
    blank = int(values["ocr.blank_index"])
    steps = int(values["ocr.expected_timesteps"])
    classes = int(values["ocr.expected_classes"])
    if (not charset or len(charset) != len(set(charset)) or
        len(charset)+1 != classes or not 0 <= blank < classes or not 0 < steps <= 1024):
        raise ValueError("Invalid OCR charset, blank or tensor contract")
    return charset, blank, steps, classes

def greedy_text(logits, charset, blank):
    output, last = [], -1
    for cls in logits.argmax(axis=1):
        cls = int(cls)
        if cls != blank and cls != last:
            output.append(charset[cls if cls < blank else cls-1])
        last = cls
    return "".join(output)

def reference_alignment(logits, truth, charset, blank):
    """Best CTC path forced to known truth, with correct repeated-label rules."""
    if not truth or any(c not in charset for c in truth):
        raise ValueError("Reference includes invalid OCR symbols")
    if (logits.ndim != 2 or logits.shape[1] != len(charset)+1 or
        logits.shape[0] < len(truth) or not np.isfinite(logits).all()):
        raise ValueError("Invalid CTC shape/data or impossible reference length")
    indices = {c: i if i < blank else i+1 for i,c in enumerate(charset)}
    labels = [blank]
    for c in truth:
        labels.extend([indices[c], blank])
    tcount, scount = logits.shape[0], len(labels)
    shifted = logits.astype(np.float64) - logits.max(axis=1, keepdims=True)
    logp = shifted - np.log(np.exp(shifted).sum(axis=1, keepdims=True))
    dp = np.full((tcount, scount), -np.inf)
    back = np.full((tcount, scount), -1, dtype=np.int32)
    dp[0,0] = logp[0,blank]
    dp[0,1] = logp[0,labels[1]]
    for t in range(1,tcount):
        for state, label in enumerate(labels):
            options = [(dp[t-1,state],state)]
            if state:
                options.append((dp[t-1,state-1],state-1))
            if state >= 2 and label != blank and label != labels[state-2]:
                options.append((dp[t-1,state-2],state-2))
            value, predecessor = max(options, key=lambda item:item[0])
            if np.isfinite(value):
                dp[t,state] = value + logp[t,label]
                back[t,state] = predecessor
    state = max((scount-2,scount-1), key=lambda s:dp[-1,s])
    if not np.isfinite(dp[-1,state]):
        raise ValueError("CTC reference path impossible")
    path = np.empty(tcount,dtype=np.int32)
    for t in range(tcount-1,-1,-1):
        path[t] = state
        if t:
            state = int(back[t,state])
            if state < 0:
                raise ValueError("Broken CTC alignment traceback")
    probs = np.exp(logp)
    chars = []
    for position, char in enumerate(truth):
        assigned = np.flatnonzero(path == 2*position+1)
        if not assigned.size:
            raise ValueError("CTC reference character not aligned")
        token = indices[char]
        timestep = int(max(assigned,key=lambda t:probs[t,token]))
        targetp = float(probs[timestep,token])
        winning = int(probs[timestep].argmax())
        winner = "<blank>" if winning == blank else charset[winning if winning < blank else winning-1]
        rank = 1 + int((probs[timestep] > targetp+1e-12).sum())
        chars.append(dict(
            targetIndex=position,character=char,
            alignedTimesteps=assigned.tolist(),bestAlignedTimestep=timestep,
            targetProbabilityAtAlignment=round(targetp,6),
            blankProbabilityAtAlignment=round(float(probs[timestep,blank]),6),
            topTokenAtAlignment=winner,targetRankAtAlignment=rank,
            targetTop1AtAnyAlignedStep=any(int(probs[t].argmax()) == token for t in assigned),
            targetTop6AtAlignment=rank <= 6))
    return dict(
        referencePathLogProbability=round(float(dp[-1,path[-1]]),4),
        characters=chars,
        blankArgmaxTimesteps=int((logits.argmax(axis=1)==blank).sum()))

def edit_operations(truth, predicted):
    """Minimum edit script: reference -> CTC greedy. No invented corrections."""
    m,n = len(truth),len(predicted)
    d = [[0]*(n+1) for _ in range(m+1)]
    for i in range(m+1):d[i][0]=i
    for j in range(n+1):d[0][j]=j
    for i in range(1,m+1):
        for j in range(1,n+1):
            d[i][j]=min(d[i-1][j-1]+(truth[i-1]!=predicted[j-1]),
                        d[i-1][j]+1,d[i][j-1]+1)
    i,j=m,n
    ops=[]
    while i or j:
        if i and j and truth[i-1]==predicted[j-1] and d[i][j]==d[i-1][j-1]:
            i-=1;j-=1
        elif i and j and d[i][j]==d[i-1][j-1]+1:
            ops.append(dict(type="substitution",referenceIndex=i-1,
                            expected=truth[i-1],predicted=predicted[j-1]))
            i-=1;j-=1
        elif i and d[i][j]==d[i-1][j]+1:
            ops.append(dict(type="deletion",referenceIndex=i-1,expected=truth[i-1]))
            i-=1
        else:
            if not j or d[i][j]!=d[i][j-1]+1:
                raise ValueError("Invalid edit traceback")
            ops.append(dict(type="insertion",referenceIndex=i,predicted=predicted[j-1]))
            j-=1
    return list(reversed(ops))

def input_rows(path,kind):
    with Path(path).open(encoding="utf-8",newline="") as f:
        for row in csv.reader(f,delimiter="\t"):
            if not row:continue
            if kind=="synthetic":
                if len(row)!=7:raise ValueError("Synthetic input must have 7 columns")
                variant,file,truth,blob,greedy,blanks,_=row
            else:
                if len(row)!=6:raise ValueError("Real input must have 6 columns")
                file,truth,blob,greedy,blanks,_=row
                variant="yolo_box"
            yield dict(variant=variant,file=file,truth=truth,blob=blob,
                       greedy=greedy,blanks=int(blanks))

def run(args):
    charset,blank,steps,classes=load_contract(args.contract)
    rows=list(input_rows(args.rows,args.kind))
    if not rows:raise ValueError("No OCR model outputs")
    native=json.loads(args.native_report.read_text(encoding="utf-8"))
    native_cases=native["cases"]
    native_by_key={(c.get("variant","yolo_box"),c["file"]):c for c in native_cases}
    if len(native_by_key)!=len(native_cases):raise ValueError("Duplicate native result keys")
    results=[]
    groups=defaultdict(list)
    for row in rows:
        key=(row["variant"],row["file"])
        if key not in native_by_key:raise ValueError("ONNX and C++ reports do not match")
        native_case=native_by_key[key]
        expected_native=native_case["truth"] if args.kind=="synthetic" else native_case["expected"]
        if expected_native!=row["truth"]:raise ValueError("Ground truth does not match")
        path=Path(row["blob"])
        if not path.is_file() or path.stat().st_size!=steps*classes*4:
            raise ValueError("Missing/wrong-shape ONNX float32 logits")
        logits=np.fromfile(path,dtype="<f4").reshape(steps,classes)
        if not np.isfinite(logits).all():raise ValueError("Nonfinite ONNX output")
        greedy=greedy_text(logits,charset,blank)
        if greedy!=row["greedy"] or int((logits.argmax(axis=1)==blank).sum())!=row["blanks"]:
            raise ValueError("Python and native model greedy/blank traces differ")
        diagnosis=reference_alignment(logits,row["truth"],charset,blank)
        operations=edit_operations(row["truth"],greedy)
        for edit in operations:
            if edit["type"] in ("deletion","substitution"):
                edit["alignment"]=diagnosis["characters"][edit["referenceIndex"]]
        top1=native_case.get("beamTop1","")
        diagnosis.update(dict(
            file=row["file"],variant=row["variant"],reference=row["truth"],
            greedy=greedy,greedyEdits=operations,beamTop1=top1,
            beamTop1RawScore=native_case.get("beamTop1Confidence"),
            beamTop1Correct=top1==row["truth"],
            beamTruthInTop5=bool(native_case.get("beamTruthInTopK",
                                                native_case.get("truthInTop5",False)))))
        results.append(diagnosis)
        groups[row["variant"]].append(diagnosis)
    if len(results)!=len(native_cases):raise ValueError("Missing native C++ cases")
    stats={}
    for variant, cases in sorted(groups.items()):
        counts=Counter(edit["type"] for case in cases for edit in case["greedyEdits"])
        deletions=[edit for case in cases for edit in case["greedyEdits"]
                   if edit["type"]=="deletion"]
        stats[variant]=dict(
            cases=len(cases),greedyExact=sum(c["reference"]==c["greedy"] for c in cases),
            beamTop1Exact=sum(c["beamTop1Correct"] for c in cases),
            beamTruthInTop5=sum(c["beamTruthInTop5"] for c in cases),
            greedyEditCounts=dict(counts),
            missingTargetRankGreaterThanOne=sum(e["alignment"]["targetRankAtAlignment"]>1
                                                 for e in deletions),
            missingBlankWinsAlignedStep=sum(e["alignment"]["topTokenAtAlignment"]=="<blank>"
                                            for e in deletions))
    report=dict(schemaVersion=1,kind="ground-truth-ctc-viterbi-diagnostic",
                inferenceAltered=False,notCalibratedAccuracy=True,dataset=args.kind,
                summary=stats,cases=results)
    args.json_out.parent.mkdir(parents=True,exist_ok=True)
    args.md_out.parent.mkdir(parents=True,exist_ok=True)
    args.json_out.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    lines=["# CTC character-loss diagnosis (actual ONNX logits)","",
           "Ground-truth-forced Viterbi is OFFLINE analysis, not inference.",
           "Model softmax and C++ beam scores are not calibrated accuracy.","",
           "| Variant | Cases | Greedy exact | Beam exact | Truth top-5 | Greedy deletions | Blank wins deletions |",
           "|---|---:|---:|---:|---:|---:|---:|"]
    for name,s in stats.items():
        lines.append(f"| {name} | {s['cases']} | {s['greedyExact']} | {s['beamTop1Exact']} | {s['beamTruthInTop5']} | {s['greedyEditCounts'].get('deletion',0)} | {s['missingBlankWinsAlignedStep']} |")
    lines.extend(["","## Per-case missing characters","",
                  "| Image | Variant | Reference | Greedy | Beam top-1 | Missing character evidence |",
                  "|---|---|---|---|---|---|"])
    for case in results:
        fragments=[]
        for op in case["greedyEdits"]:
            if op["type"]!="deletion":continue
            a=op["alignment"]
            fragments.append(
                f"{op['expected']}@{op['referenceIndex']} t={a['bestAlignedTimestep']} "
                f"p={a['targetProbabilityAtAlignment']:.3f} "
                f"blank={a['blankProbabilityAtAlignment']:.3f} "
                f"winner={a['topTokenAtAlignment']} rank={a['targetRankAtAlignment']}")
            print("CTC_MISSING",case["file"],case["variant"],fragments[-1],flush=True)
        lines.append(f"| {case['file']} | {case['variant']} | {case['reference']} | {case['greedy']} | {case['beamTop1']} | {'; '.join(fragments) or 'none'} |")
    args.md_out.write_text("\n".join(lines)+"\n",encoding="utf-8")
    print("CTC_DIAG_SUMMARY",json.dumps(stats),flush=True)

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rows",type=Path,required=True)
    p.add_argument("--kind",choices=("real","synthetic"),required=True)
    p.add_argument("--contract",type=Path,required=True)
    p.add_argument("--native-report",type=Path,required=True)
    p.add_argument("--json-out",type=Path,required=True)
    p.add_argument("--md-out",type=Path,required=True)
    run(p.parse_args())
