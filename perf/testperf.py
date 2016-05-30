#!/usr/bin/env python

import argparse
import itertools
import subprocess
import sys

def create_parser():
    parser = argparse.ArgumentParser(prefix_chars = '-')
    parser.add_argument('--args', '-a', nargs = '*')
    parser.add_argument('--filter','-f')  
    parser.add_argument('--quiet', '-q', action = 'store_true', dest = 'quiet')
    parser.add_argument('--kelf', '-k', required = True)

    return parser

def parse_kelf_args(kargs):
    print(kargs)
    if len(kargs) == 0:
        return []
    ret = []
    arg_name = None
    for i in range(len(kargs)):
        if kargs[i][0] != '[':
            # it's an argument name
            arg_name = kargs[i]
        else:
            if arg_name == None:
                pass
            # it is a range or a value set
            if kargs[i][-1] != ']':
                # TODO error unexpected syntax
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
                        ret.append((arg_name, rng))
                        arg_name = None
                    except ValueError:
                        # TODO handle error syntax
                        pass
            elif ',' in vals:
                svals = vals.split(',')
                if len(svals) == 0:
                    # TODO handle set syntax error
                    pass
                ret.append((arg_name, svals))
                arg_name = None
            else:
                ret.append((arg_name, vals))
                arg_name = None
    return ret

def construct_bench(params):
    ll = []
    for i in range(len(params)):
        ll.append(params[i][1])
    return list(itertools.product(*ll))


def main():
    parser = create_parser()
    args = parser.parse_args()
    kelf_args_matrix = parse_kelf_args(args.args)
    print(kelf_args_matrix)
    m = construct_bench(kelf_args_matrix)
    print(m)
    out_fltr = args.filter
    for i in range(len(m)):
        command = 'k1-jtag-runner --exec-file=Cluster0:' + args.kelf + ' --'
        for j in range(len(m[i])):
            command = command + ' --' + kelf_args_matrix[j][0] + ' ' + m[i][j]
        print(command)
        subprocess.call(command, shell=True)

if __name__ == '__main__':
    main()
