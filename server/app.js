const express = require('express');
const app = express();
var port = process.env.PORT || 8000;

app.get('/aqi/:data',function(req,res){
        var data = req.params["data"];
        console.log(data);
	res.send(200);
});

app.listen(port);
