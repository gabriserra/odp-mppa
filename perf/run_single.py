#!/usr/bin/env python

from __future__ import print_function

import argparse
import itertools
import re
import subprocess

YES = ['yes', 'Yes', 'YES', 'y', 'Y', 'true', 'True', 'TRUE', 't', 'T']
NO  = ['no', 'No', 'NO', 'n', 'N', 'false', 'False', 'FALSE', 'f', 'F']

quiet = False

def printq(msg, end = '\n', cmnt = True):
    if not quiet:
        m = msg
        if cmnt:
            m = '# ' + msg
        print(m, end = end)


def create_parser():
    parser = argparse.ArgumentParser(prefix_chars = '-')
    parser.add_argument('kelf')
    parser.add_argument('--args', '-a', nargs = '*', default = [])
    parser.add_argument('--quiet', '-q', action = 'store_true')
    parser.add_argument('--regex', '-r', action = 'append', default = [])
    return parser


def parse_kelf_args(kargs):
    if len(kargs) == 0:
        return {}
    ret = {}
    arg_name = None
    for i in range(len(kargs)):
        if kargs[i][0] != '[':
            # it's an argument name
            arg_name = kargs[i].strip()
        else:
            if arg_name == None:
                pass
            # it is a range or a value set
            if kargs[i][-1] != ']':
                raise 'values must be enclosed in brackets'
                pass
            vals = kargs[i][1:-1]
            # try range
            if ':' in vals:
                svals = vals.split(':')
                if len(svals) == 3:
                     # range syntax
                    try:
                        start = int(svals[0])
                        stop = int(svals[1])
                        step = int(svals[2])
                        rng = range(start, stop, step)
                        ret[arg_name] = rng
                        arg_name = None
                    except ValueError:
                        raise 'a range of values must have integer boundaries'
                        pass
            elif '_' in vals:
                svals = vals.split('_')
                if len(svals) == 0:
                    raise 'Invalid syntax'
                    pass
                ret[arg_name] = svals
                arg_name = None
            else:
                ret[arg_name] = [vals]
                arg_name = None
    return ret


def permute_args(parsed_kelf_args):
    ll = []
    for arg_name in parsed_kelf_args:
        ll.append(parsed_kelf_args[arg_name])
    return list(itertools.product(*ll))


def build_command(kelf_file_name, arg_names, arg_values):
    command = ['k1-jtag-runner']
    command.append('--exec-file=Cluster0:' + kelf_file_name)
    command.append('--')
    for i in range(len(arg_values)):
        n = arg_names[i]
        v = arg_values[i]
        arg_prefix = '--'
        if len(n) == 1:
            arg_prefix = '-'
        arg_str = ''
        if v in YES:
            arg_str = arg_prefix + n
            command.append(arg_str)
        elif v in NO:
            command.append(arg_str)
            continue
        else:
            arg_str = arg_prefix + n
            command.append(arg_str)
            command.append(str(v))
    return command


def get_test_name_with_args(kelf_file_name, arg_names, arg_values):
    t = kelf_file_name.split('/')[-1]
    ret = t[: t.rfind('.')]
    for i in range(len(arg_values)):
       v = arg_values[i]
       if v in YES:
            ret = ret + '_' + arg_names[i]
            continue
       if v in NO:
            continue
       ret = ret + '_' + arg_names[i] + ':' + str(v)
    return ret

        
def main():
    parser = create_parser()
    script_args = parser.parse_args()
    global quiet
    quiet = script_args.quiet
    kelf_args_matrix = parse_kelf_args(script_args.args)
    permutations = permute_args(kelf_args_matrix)
    regex_args = script_args.regex

    for i in range(len(permutations)):
        command = build_command(script_args.kelf, kelf_args_matrix.keys(), permutations[i])
        printq('-' * 80)
        printq('Running test: ' + ' '.join(command))
        printq('-' * 80)
        job = subprocess.Popen(command, stdout = subprocess.PIPE)

        stdoutdata = ''
        try:
            while True:
                line = job.stdout.readline()
                if line:
                    stdoutdata = stdoutdata + line
                    printq(line, end = '')
                else:
                    break
        except KeyboardInterrupt:
            # Subprocess stays alive when script is interrupted!
            job.terminate()
            break
        job.wait()

        matched_results = {}
        for r in regex_args:
            l = r.split('::')
            exp = l[0]
            compiled_exp = re.compile(exp)
            match = compiled_exp.search(str(stdoutdata))
            if match:
                for j in range(1, compiled_exp.groups + 1):
                    metric = l[j]
                    matched_results[metric] = match.group(j)

        test_name = get_test_name_with_args(script_args.kelf, kelf_args_matrix.keys(), permutations[i])
        for metric in matched_results:
            print(metric + ' ' + test_name + ' ' + matched_results[metric])
        

if __name__ == '__main__':
    main()
