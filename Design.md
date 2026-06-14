**SOLD OpenDDS Project**

*Purpose*
The purpose of this project is to evolve into a c++ library complete with test suits, unit tests only where practical, and high quality Documentation for users.  OpenDDS is a notoriously large dependency which should be insulated from the user!

*Plan*
Based on this succes of this project as is, we want to evolve in an iterative manner into a high quality SOLID Design Software Module.

* single point of entry into OpenDDS using a service-base class that is exposed to the user, all other code should be hidden where practical.
* Service based class should follow the pre activate, post activate, design pattern.
* Users should be able to register topics to publish on and register lambdas to handle received messages over topics.