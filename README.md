this is a database i am making for fun.
it was inspired by lisp and sql.
what dbll is good at is what sql sucks at and vice versa.
sql is good with tables and really long columns really fast.
i actually use plan to use sql for my personal finances.
however, sql is bad at trees, you have to use ad-hoc tables.
dbll is really good at this, in fact that's its specialty.
creating/deleting/finding lists (branches of tree) is costly because it uses a lot of pointers, so lots of cache misses, so it's advised to use them sparingly.
allocating data is done into blocks, which are the size of lists, and have $PTR_SIZE bytes allocated at the end of the list to link to the next block, until it hits null.
because of this, use the same block for a long time before moving on to another one, so as to minimize cache misses.
if you plan to use this in a project of yours, be aware that this project is not finished as of yet, and bugs will occur.
there is a testing suite to help with this though, the testing suite also doubles as example code for the dbll api.
if you're going to use this in production and accept the trouble it will give, please be advised by these rules:
	respect everyones privacy.
	think before you type.
	with great power comes great responsibility.

for pull requests, issues, and other things of that nature. treat everyone equally.
if someones code or idea is not the best for this project, be polite, respectful, and technical about it.
anyone who doesn't respect this will not be allowed onto this project, talking in pull requests, issues, etc. 
if you are a technically competent person in regards to this project, of any race, ethnicity, gender, sexuality, etc. and you believe you can help this project, please do so.
