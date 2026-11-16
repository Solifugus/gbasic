' A library with its own database and statements (docs/gsql_library_blocks_design.md).
' Its functions reach them by bare name, in every form a program's functions can.
library store
    load gsql

    ' wait: 600 -- these run under valgrind on s390x (QEMU), where an owner took
    ' longer than the default 10 s to answer; it is only how long a slow one may take
    database shop( file: "store.gsql", create: true, wait: 600 )

    sql make on shop
        create table item (name string primary key, price integer)
    end sql

    sql add on shop
        insert into item (name, price) values (:name, :price)
    end sql

    sql all_items on shop
        select name, price from item order by name
    end sql

    sql cheaper on shop
        select name from item where price < :limit order by name
    end sql

    function setup()
        make.exec()
    end function

    ' :name and :price are this function's parameters
    function put(name, price)
        add.exec()
    end function

    ' a record supplies the parameters too, and gsql.exec takes the block
    function put_record(r)
        gsql.exec(add, r)
    end function

    function names()
        out = []
        for each row in all_items
            append(out, row.name)
        end for
        return out
    end function

    function under(limit)
        out = []
        for each row in cheaper.query()
            append(out, row.name)
        end for
        return out
    end function

    function close()
        shop.close()
    end function
end library
