' A second library with a `database shop` of its own: two databases, not one.
library depot
    load gsql

    ' wait: 600 -- these run under valgrind on s390x (QEMU), where an owner took
    ' longer than the default 10 s to answer; it is only how long a slow one may take
    database shop( file: "depot.gsql", create: true, wait: 600 )

    sql make on shop
        create table bin (code string primary key)
    end sql

    sql add on shop
        insert into bin (code) values (:code)
    end sql

    sql codes on shop
        select code from bin order by code
    end sql

    function setup()
        make.exec()
    end function

    function put(code)
        add.exec()
    end function

    function all_codes()
        out = []
        for each row in codes
            append(out, row.code)
        end for
        return out
    end function
end library
