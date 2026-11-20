package main
import ("context";"fmt";"os";"github.com/jackc/pgx/v5")
func main(){
 ctx:=context.Background()
 c,err:=pgx.Connect(ctx,"postgres://127.0.0.1:"+os.Args[1]+"/pgcap?sslmode=disable"); if err!=nil{fmt.Println(err);os.Exit(1)}
 var name string
 if err:=c.QueryRow(ctx,"select name from customer where id = $1",1).Scan(&name);err!=nil{fmt.Println(err);os.Exit(1)}
 rows,_:=c.Query(ctx,"select total, issued from invoice"); for rows.Next(){}; rows.Close()
 c.Close(ctx)
}
