// Copyright (c) 2020 Can Boluk and contributors of the VTIL Project   
// All rights reserved.   
//    
// Redistribution and use in source and binary forms, with or without   
// modification, are permitted provided that the following conditions are met: 
//    
// 1. Redistributions of source code must retain the above copyright notice,   
//    this list of conditions and the following disclaimer.   
// 2. Redistributions in binary form must reproduce the above copyright   
//    notice, this list of conditions and the following disclaimer in the   
//    documentation and/or other materials provided with the distribution.   
// 3. Neither the name of VTIL Project nor the names of its contributors
//    may be used to endorse or promote products derived from this software 
//    without specific prior written permission.   
//    
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE   
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE  
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE   
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR   
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF   
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN   
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)   
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE  
// POSSIBILITY OF SUCH DAMAGE.        
//
#include "bblock_thunk_removal_pass.hpp"
#include <algorithm>

namespace vtil::optimizer
{
	// Implement the pass.
	//
	size_t bblock_thunk_removal_pass::pass(basic_block* blk, bool xblock)
	{
		fassert( xblock );

		// Track at what block we start the recursive fixing.
		// We can only delete blocks just before leaving the function
		//
		if(!first_block)
			first_block = blk;

		// Skip if already visited.
		//
		if ( !visited.emplace( blk ).second )
			return 0;

		size_t counter = 0;		

		// Check if the only instruction is a jump imm to the next basic block
		// Also make sure that the block is only referenced by one previous block
		//
		if (blk->size() == 1 &&			//only one instruction
			blk->next.size() == 1 &&	//only accessed from one path
			blk->prev.size() == 1 &&	//only jumping to one destination				
			blk->back().base->is_branching_virt())
		{
			fassert(blk->front() == blk->back());

			// This should never happen in real world cases. We check for "jmp only", which implies zero changes to the stack
			// This might happen if you write a test case, but shouldn't be possible otherwise
			//
			fassert(blk->sp_offset == 0);

			basic_block* next = blk->next[0];
			basic_block* prev = blk->prev[0];
			bool can_fold = prev != next;

			if (can_fold)
			{
				auto prev_branch = prev->back();
				fassert(prev_branch.base->is_branching_virt());

				// If any virtual branch target operand is register-based, we cannot
				// safely retarget old_vip->new_vip for this predecessor.
				//
				for (int idx : prev_branch.base->branch_operands_vip)
				{
					fassert(idx >= 0 && (size_t)idx < prev_branch.operands.size());
					if (!prev_branch.operands[idx].is_immediate())
					{
						can_fold = false;
						break;
					}
				}
			}

			size_t rewritten_targets = 0;
			if (can_fold)
			{
				// Retarget only branch target operands and ensure at least one
				// successful rewrite happened before touching CFG links.
				//
				auto& mut_branch = make_mutable(prev->back());
				for (int idx : mut_branch.base->branch_operands_vip)
				{
					fassert(idx >= 0 && (size_t)idx < mut_branch.operands.size());
					auto& op = mut_branch.operands[idx];
					if (op.is_immediate() && op.imm().ival == blk->entry_vip)
					{
						op.imm().ival = next->entry_vip;
						rewritten_targets++;
					}
				}

				can_fold = rewritten_targets != 0;
			}

			if (can_fold)
			{
				auto dedup_links = [] (auto& links)
				{
					for (auto it = links.begin(); it != links.end();)
					{
						if (std::find(links.begin(), it, *it) != it)
							it = links.erase(it);
						else
							++it;
					}
				};

				// Rewire predecessor/successor links and deduplicate adjacency.
				//
				prev->next.erase(std::remove(prev->next.begin(), prev->next.end(), blk), prev->next.end());
				if (std::find(prev->next.begin(), prev->next.end(), next) == prev->next.end())
					prev->next.emplace_back(next);
				dedup_links(prev->next);

				next->prev.erase(std::remove(next->prev.begin(), next->prev.end(), blk), next->prev.end());
				if (std::find(next->prev.begin(), next->prev.end(), prev) == next->prev.end())
					next->prev.emplace_back(prev);
				dedup_links(next->prev);

				// TODO: should we do this with another operands loop? We currently only have "js" that qualifies so a simple if does the job for now
				//
				auto branching_instruction = prev->back();
				if (branching_instruction.base == &ins::js)
				{
					fassert(branching_instruction.operands.size() == 3);

					// This pass should not interfer with blocks that aren't already touched by branch correction / our corrections above
					//
					if (branching_instruction.operands[1].is_immediate() && branching_instruction.operands[2].is_immediate())
					{
						if (branching_instruction.operands[1].imm().ival == branching_instruction.operands[2].imm().ival)
						{
							auto new_vip = branching_instruction.operands[1].imm();

							auto ins = std::prev(prev->end());

							(+ins)->base = &ins::jmp;
							(+ins)->operands.resize(1);
							(+ins)->operands[0] = { new_vip.ival, arch::bit_count };

							prev->next.clear();
							prev->next.emplace_back(next);
							dedup_links(next->prev);
						}
					}
				}

				obsolete_blocks.emplace(blk);
				counter++;
			}
		}

		// Recurse into destinations:
		//
		for ( auto* dst : blk->next )
			counter += pass( dst, true );

		// Remove queued obsolete blocks
		// Use the first_block variable we saved before to detect recursive depth
		//
		auto rtn = blk->owner;
		if (first_block == blk)
		{			
			for (auto it : obsolete_blocks)
				rtn->delete_block(const_cast<vtil::basic_block*>(it));
		}

		return counter;
	}
	size_t bblock_thunk_removal_pass::xpass(routine* rtn)
	{
		// Invoke recursive optimizer starting from entry point.
		//
		visited.clear();
		obsolete_blocks.clear();
		first_block = nullptr;
		visited.reserve( rtn->num_blocks() );
		return pass( rtn->entry_point, true );
	}
};
