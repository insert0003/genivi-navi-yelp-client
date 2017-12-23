# AGL Reference POI application
### This application provides the function of destination search to AGL.  It uses the API provided by AGL Reference Navigation.  This application uses yelp WebAPI.

### The settings required to run POI application are as follows:

#### 1. Please connect to the Internet. (Because POI-App uses yelp's web-API)

#### 2. Please register yelp

##### https://www.yelp.co.jp/developers?country=US
##### Please do developer registration and obtain App ID and App Secret key.
##### And please write it in /etc/poikey

##### The contents are as follows (2 lines):
###### your app id
###### your sercret key

### Restrictions：
#### POI App uses Navigation API. Push Navigation button before POI app.
