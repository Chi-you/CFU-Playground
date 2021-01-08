#!/bin/env python
# Copyright 2020 Google LLC
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     https://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from nmigen import *
from nmigen_cfu import SimpleElaboratable, InstructionBase, TestBase, InstructionTestBase, Cfu

import unittest
class WidthHeight(SimpleElaboratable):
    def __init__(self):
        self.max_width = Signal(signed(32))
        self.max_height = Signal(signed(32))

    def elab(self, m):
        pass

class InitInstruction(InstructionBase):
    def __init__(self):
        super().__init__()
        self.max_width = Signal(signed(32))
        self.max_height = Signal(signed(32))

    def elab(self, m):
        with m.If(self.start):
            m.d.sync += [
                self.max_width.eq(self.in0),
                self.max_height.eq(self.in1),
            ]
            m.d.comb += [
                self.done.eq(1),
                self.output.eq(1),
            ]
        return m

class InitInstructionTest(TestBase):
    def create_dut(self):
        return InitInstruction()

    def test_init(self):
        DATA = [
            ((1,0,0),(1,1,0,0)),
            ((0,1,1),(0,0,0,0)),
            ((0,5,6),(0,0,0,0)),
            ((1,1,1),(1,1,1,1)),
            ((0,1,1),(0,0,1,1)),
            ((0,0,0),(0,0,1,1)),
            ((0,2,3),(0,0,1,1)),
            ((1,2,3),(1,1,2,3)),
            ((0,1,1),(0,0,2,3)),
        ]
        def process():
            for n,(inputs,outputs) in enumerate(DATA):
                start,in0,in1 = inputs
                done, output,width,height = outputs
                yield self.dut.start.eq(start)
                yield self.dut.in0.eq(in0)
                yield self.dut.in1.eq(in1)
                yield
                # Width and height will change in the next yield
                if (start == 1):
                    yield
                self.assertEqual((yield self.dut.done),start)
                self.assertEqual((yield self.dut.output),start)
                self.assertEqual((yield self.dut.max_width),width)
                self.assertEqual((yield self.dut.max_height),height)
        self.run_sim(process, True)

class DoubleCompareInstruction(InstructionBase):
    """The 4 comparisons will be performed in parallel
    """
    def __init__(self):
        super().__init__()
        self.max_width = Signal(signed(32))
        self.max_height = Signal(signed(32))

    def elab(self, m):
        m.d.comb += [
            self.output.eq((self.in0 > 0) & (self.in0 < self.max_width) & (self.in1 > 0) & (self.in1 < self.max_height)),
            self.done.eq(1)
        ]

class DoubleCompareInstructionTest(TestBase):
    def create_dut(self):
        return DoubleCompareInstruction()

    def test_double_compare(self):
        DATA = [
            ((0,0),(1,2,2,1)),
            ((4,5),(1,4,5,1)),
            ((19,20),(1,10,10,0)),
            ((11,13),(1,11,12,0)),
            ((18,20),(1,19,21,1)),
            ((-3,1),(1,5,6,0)),
            ((-2,-2),(1,5,6,0)),
        ]
        def process():
            for n,(inputs,outputs) in enumerate(DATA):
                in0,in1 = inputs
                done,width,height,output = outputs
                yield self.dut.in0.eq(in0)
                yield self.dut.in1.eq(in1)
                yield self.dut.max_width.eq(width)
                yield self.dut.max_height.eq(height)
                yield
                if ((in0 > 0) & (in0 < width) & (in1 > 0) & (in1 < height)):
                    self.assertEqual((yield self.dut.output),1)
                else:
                    self.assertEqual((yield self.dut.output),0)
        self.run_sim(process, True)

def make_cfu():
    return Cfu({0: DoubleCompareInstruction()})

if __name__ == '__main__':
    unittest.main()
