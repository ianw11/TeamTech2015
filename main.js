var phantom = require("phantomcss/phantomcss.js");
var api = require("resemblejs/resemble.js");

var myFunc = function() {

   console.log("Running javascript program with node");

   var diff = api.resemble("People1.jpg").compareTo("People2.jpg").ignoreColors().onComplete( function(data) {
      console.log(data);
   });

};

myFunc();
