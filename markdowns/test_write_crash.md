# Testing Write Crash

## Issue 1: Storage Server Crash on "0 interrupt"

**Steps to reproduce:**
1. Start nameserver
2. Start storageserver
3. WRITE newfile.txt 0
4. Type: `0 interrupt`
5. Storage server disconnects

**Root cause investigation:**
- Added NULL safety check in flatten_list_to_file()
- Need to test if this fixes the crash

## Issue 2: "..." Should Create Multiple Sentences

**Expected behavior:**
```
WRITE test.txt 0
0 ...
ETIRW
```

Should create 3 sentences (one for each period):
- Sentence 0: "."
- Sentence 1: "."
- Sentence 2: "."

**Current behavior:**
Treats "..." as a single word in sentence 0

**Solution:**
Created `create_delimiter_sentences()` function to split consecutive delimiters.
Need to integrate it into insert_word_at().

## Test Plan

1. **First**: Test if crash is fixed with current safety check
2. **Then**: Implement "..." splitting if needed
