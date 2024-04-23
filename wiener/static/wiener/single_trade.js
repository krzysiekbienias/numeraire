

'use strict'
const single_trade_url=document.URL

console.log(document.URL)
const postForm=document.getElementById("market-data-form")
const csrf=document.getElementsByName('csrfmiddlewaretoken')
const pv_dict=document.getElementById("pv")
var html_content=document.getElementById('res')
var price_trade=document.getElementById('price-trade')



// --------------------------
// Region Form
// --------------------------

// --------------------------
// End Region Form
// --------------------------

$("#price-trade-btn").click(function (e){
    e.preventDefault();
    const valuation=document.getElementById('id_valuation_date')
    const risk_free_rate=document.getElementById('id_risk_free_rate')
    const volatility=document.getElementById('id_volatility')
    $.ajax({
        type:"POST",
        url:'',
        data:{
            "csrfmiddlewaretoken":csrf[0].value,
            "valuation_date":valuation.value,
            "risk_free_rate":risk_free_rate.value,
            "volatility":volatility.value
        },
        dataType:'json',
        success:function(data){
            price_trade.innerHTML=data.price_trade.toFixed(2)

        },

        error:function(error){
            console.log(error)
        }
    })

})


$("#pv-able-btn").click(function (e){
    e.preventDefault()
    const valuation=document.getElementById('id_valuation_date')
    const risk_free_rate=document.getElementById('id_risk_free_rate')
    const volatility=document.getElementById('id_volatility')

    $.ajax({
        type: "POST",
        url: '',
        data: {
            "csrfmiddlewaretoken": csrf[0].value,
            "valuation_date": valuation.value,
            "risk_free_rate": risk_free_rate.value,
            "volatility": volatility.value
        },
        dataType: 'json',
        success: function (data) {
            var tableData = '<h4 class="ui horizontal divider header">' +
                '<i class="tag icon"></i>Pvable dictionary</h4>' +
                '<p>PVAble dictionary is a dictionary to store everything data for pricing.' +

                "It is a combination market data and trade attributes," +
                'supported by additional calulations.</p>' +
                '<h4 class="ui horizontal divider header">' +
                '<i class="database icon"></i>Pvable' +
                '</h4>'
            tableData += '<table class="ui definition table">'
            $.each(data.pv_dict, function (key, value) {
                tableData += '<tr>';
                tableData += '<td class="two wide column">' + key + '</td>';
                tableData += '<td>' + value + '</td>';
                tableData += '</tr>';
            });
            tableData += '</table>';
            $('#res').html(tableData);

        },

        error: function (error) {
            console.log(error)
        }

    })
})


const backBtn=document.getElementById("back-btn")

backBtn.addEventListener('click',()=>{
    history.back()
})




