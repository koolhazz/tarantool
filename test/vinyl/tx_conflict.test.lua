-- The test runs loop of given number of rounds.
-- Every round does the following:
-- The test starts several concurrent transactions in vinyl.
-- The transactions make some read/write operations over several keys in
--  a random order and commit at a random moment.
-- After that all transactions are sorted in order of commit
-- With the sublist of read-write transactions committed w/o conflict:
-- Test tries to make these transactions in memtex, one tx after another,
--  without interleaving and compares select results with vinyl to make sure
--  if the transaction could be serialized in order of commit or not
-- With the sublist of read-write transactions committed with conflict:
-- Test tries to make the same operations end ensures that the read results
--  are not possible in memtx.
-- With the sublist of read only transactions:
-- Test tries to insert these transactions between other transactions and checks
--  that it possible to get same results.

test_run = require('test_run').new()
txn_proxy = require('txn_proxy')
--settings
num_tx = 10 --number of concurrent transactions
num_key = 5 --number of keys that transactions use
num_tests = 50 --number of test rounds to run

txs = {}
order_of_commit = {}
num_committed = 0
stmts = {}
errors = {}
ops = {'begin', 'commit', 'select', 'replace', 'upsert', 'delete'}

-- ignore case of unnecessary conflict:
-- s:delete{1}
-- t1:begin() t1:select{1} t1:replace{2} s:replace{1} s:delete{1} t1:commit()
ignore_unnecessary_conflict1 = true

--avoid first upsert in transaction
--fails if num_tests = 1000
ignore_unnecessary_conflict2 = true

test_run:cmd("setopt delimiter ';'")

s1 = box.schema.create_space('test1', { engine = 'vinyl' })
i1 = s1:create_index('test', { type = 'TREE', parts = {1, 'uint'} })
s2 = box.schema.create_space('test2', { engine = 'memtx' })
i2 = s2:create_index('test', { type = 'TREE', parts = {1, 'uint'} })
if ignore_unnecessary_conflict1 then
    q1 = box.schema.create_space('testq1', { engine = 'vinyl' })
    iq1 = q1:create_index('test', { type = 'TREE', parts = {1, 'uint'} })
    q2 = box.schema.create_space('testq2', { engine = 'memtx' })
    iq2 = q2:create_index('test', { type = 'TREE', parts = {1, 'uint'} })
end;

for i=1,num_tx do
    txs[i] = {con = txn_proxy.new()}
end;

function my_equal(a, b)
    local typea = box.tuple.is(a) and 'table' or type(a)
    local typeb = box.tuple.is(b) and 'table' or type(b)
    if typea ~= typeb then
        return false
    elseif typea ~= 'table' then
        return a == b
    end
    for k,v in pairs(a) do if not my_equal(b[k], v) then return false end end
    for k,v in pairs(b) do if not my_equal(a[k], v) then return false end end
    return true
end;

unique_value = 0
function get_unique_value()
    unique_value = unique_value + 1
    return unique_value
end;

function prepare()
    order_of_commit = {}
    num_committed = 0
    stmts = {}
    for i=1,num_tx do
        txs[i].started = false
        txs[i].ended = false
        if math.random(3) == 1 then
            txs[i].read_only = true
        else
            txs[i].read_only = false
        end
        txs[i].read_only_checked = false
        txs[i].conflicted = false
        txs[i].possible = nil
        txs[i].num_writes = 0
    end
    s1:truncate()
    s2:truncate()
    for i=1,num_key do
        local r = math.random(5)
        local v = get_unique_value()
        if (r >= 2) then
            s1:replace{i, v}
            s2:replace{i, v }
            if ignore_unnecessary_conflict1 then
                q1:replace{i, v}
                q2:replace{i, v }
            end
        end
        if (r == 2) then
            s1:delete{i}
            s2:delete{i}
        end
    end
end;

function apply(t, k, op)
    local tx = txs[t]
    local v = nil
    local q = nil
    local k = k
    if op == 'begin' then
        if tx.started then
            table.insert(errors, "assert #1")
        end
        tx.started = true
        tx.con:begin()
        k = nil
    elseif op == 'commit' then
        if tx.ended or not tx.started then
            table.insert(errors, "assert #2")
        end
        tx.ended = true
        table.insert(order_of_commit, t)
        num_committed = num_committed + 1
        local res = tx.con:commit()
        if res ~= "" and res[1]['error'] then
            tx.conflicted = true
        else
            tx.select_all = s1:select{}
            if tx.num_writes == 0 then
                tx.read_only = true
            end
        end
        k = nil
    elseif op == 'select' then
        v = tx.con('s1:select{'..k..'}')
        if ignore_unnecessary_conflict1 then
            q = tx.con('q1:select{'..k..'}')
        end
    elseif op == 'replace' then
        v = get_unique_value()
        tx.con('s1:replace{'..k..','..v..'}')
        if ignore_unnecessary_conflict1 then
            tx.con('q1:replace{'..k..','..v..'}')
        end
        tx.num_writes = tx.num_writes + 1
    elseif op == 'upsert' then
        v = math.random(100)
        tx.con('s1:upsert({'..k..','..v..'}, {{"+", 2,'..v..'}})')
        if ignore_unnecessary_conflict1 then
            tx.con('q1:upsert({'..k..','..v..'}, {{"+", 2,'..v..'}})')
        end
        tx.num_writes = tx.num_writes + 1
    elseif op == 'delete' then
        tx.con('s1:delete{'..k..'}')
        tx.num_writes = tx.num_writes + 1
    end
    table.insert(stmts, {t=t, k=k, op=op, v=v, q=q})
end;

function act()
    while true do
        local t = math.random(num_tx)
        local k = math.random(num_key)
        local tx = txs[t]
        if not tx.ended then
            local op_no = 0
            if (tx.read_only) then
                op_no = math.random(3)
            else
                op_no = math.random(6)
            end
            local op = ops[op_no]
            if ignore_unnecessary_conflict2 then
                local were_ops = false
                for i,st in ipairs(stmts) do
                    if st.t == t and st.k == k and st.op ~= 'commit' then
                        were_ops = true
                    end
                end
                if op == 'upsert' and not were_ops then
                    op = 'replace'
                end
            end
            if op ~= 'commit' or tx.started then
                if not tx.started then
                    apply(t, k, 'begin')
                end
                if op ~= 'begin' then
                    apply(t, k, op)
                end
            end
            return
        end
    end
end;

function is_rdonly_tx_possible(t)
    for _,s in pairs(stmts) do
        if s.t == t and s.op == 'select' then
            local cmp_with = {s2:select{s.k}}
            if not my_equal(s.v, cmp_with) then
                return false
            end
        end
    end
    return true
end;

function try_to_apply_tx(t)
    for _,s in pairs(stmts) do
        if s.t == t then
            if s.op == 'select' then
                local cmp_with = {s2:select{s.k}}
                if not my_equal(s.v, cmp_with) then
                    return false
                end
                if ignore_unnecessary_conflict1 then
                    cmp_with = {q2:select{s.k}}
                    if not my_equal(s.q, cmp_with) then
                        return false
                    end
                end
            elseif s.op == 'replace' then
                s2:replace{s.k, s.v}
                if ignore_unnecessary_conflict1 then
                    q2:replace{s.k, s.v }
                end
            elseif s.op == 'upsert' then
                s2:upsert({s.k, s.v}, {{'+', 2, s.v}})
                if ignore_unnecessary_conflict1 then
                    q2:upsert({s.k, s.v}, {{'+', 2, s.v}})
                end
            elseif s.op == 'delete' then
                s2:delete{s.k}
            end
        end
    end
    return true
end;

function check_rdonly_possibility()
    for i=1,num_tx do
        if txs[i].read_only and not txs[i].possible then
            if is_rdonly_tx_possible(i) then
                txs[i].possible = true
            end
        end
    end
end;

function check()
    for i=1,num_tx do
        if txs[i].read_only then
            if txs[i].conflicted then
                table.insert(errors, "read-only conflicted " .. i)
            end
            txs[i].possible = false
        end
    end
    check_rdonly_possibility()
    for _,t in ipairs(order_of_commit) do
        if not txs[t].read_only then
            if txs[t].conflicted then
                box.begin()
                if try_to_apply_tx(t) then
                    table.insert(errors, "could be serializable " .. t)
                end
                box.rollback()
            else
                if not try_to_apply_tx(t) then
                    table.insert(errors, "not serializable " .. t)
                end
                if not my_equal(txs[t].select_all, s2:select{}) then
                    table.insert(errors, "results are different " .. t)
                end
                check_rdonly_possibility()
            end
        end
    end
    for i=1,num_tx do
        if txs[i].read_only and not txs[i].possible then
            table.insert(errors, "not valid read view " .. i)
        end
    end
end;

for i = 1, num_tests do
    prepare()
    while num_committed ~= num_tx do
        act()
    end
    check()
end;

test_run:cmd("setopt delimiter ''");

errors

s1:drop()
s2:drop()
if ignore_unnecessary_conflict1 then q1:drop() q2:drop() end
